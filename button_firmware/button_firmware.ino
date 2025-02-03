/**
 * @file    secure_ble_beacon.ino
 * @brief   Secure BLE Emergency Beacon Implementation for ESP32-H2
 * @details Implements secure rolling code beacon with factory reset and sleep modes
 * 
 * @author  Saurabh Datta | Datta+Baum Studio
 * @date    2024-12 -> 2025-01
 * @version 0.0.2
 * @license GPL 3.0
 *
 * @target    ESP32-H2
 * @required  ESP32-H2 with configured GPIO pins
 * @dependencies
 *   - BLE Library
 *   - Adafruit_NeoPixel
 * @warning   Requires specific hardware configuration
 */


/**
* Imports for shared secrets for rolling code generation
*/
#include "secrets.h"

/**
 * @Note Debug Level Selection (before including debug.h)
 * @Options DEBUG_LEVEL_NONE, DEBUG_LEVEL_VERBOSE
*/
#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE
#include "debug_log.h"

/**
 * @Note Enable/Disable Debug LED (before including led.h)
 * @Options DEBUG_LED_NONE, DEBUG_LED_ENABLE
*/
#define DEBUG_LED DEBUG_LED_ENABLED
#include "debug_led.h"

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <esp_mac.h>
#include "esp_private/periph_ctrl.h"
#include "soc/periph_defs.h"
#include "driver/rtc_io.h"
#include "soc/rtc.h"




/* ============= Configuration Constants ============= */
#define PRODUCT_NAME "HELP by JENNYFER" /**< Product-specific name */
#define BOOT_PIN GPIO_NUM_9             /**< GPIO pin for BOOT button: gpio_num_t type, not a simple int */
#define BEACON_TIME_MS 10000            /**< Broadcast duration in ms */
#define FACTORY_WAIT_MS 20000           /**< Factory reset timeout in ms */



/* ============= Type Definitions ============= */
/**
 * @brief Device operating states
 * @details Defines all possible states the device can be in
 */
enum class DeviceState {
  UNINITIALIZED, /**< Initial state after power-on */
  FACTORY_MODE,  /**< Factory reset/initialization mode */
  NORMAL_MODE,   /**< Normal beacon operation mode */
  ERROR          /**< Error state */
};

/**
 * @brief Error codes for device operation
 * @details Defines all possible error conditions
 */
enum class ErrorCode {
  NONE = 0,
  BLE_INIT_FAILED,
  INVALID_STATE
};

/**
 * @brief RTC data structure
 * @details Data persisted in RTC memory across deep sleep cycles
 */
// Add a magic number to validate RTC memory initialization
#define RTC_DATA_MAGIC 0xDABBF00D  // Unique hexadecimal identifier (fixed version)
typedef struct __attribute__((packed)) {
  uint32_t magic;  // Add this to validate RTC memory
  uint32_t seed;
  uint32_t counter;
  bool is_initialized;
  DeviceState state;
  ErrorCode lastError;
} rtc_data_t;



/* ============= Global Variables ============= */
RTC_DATA_ATTR static rtc_data_t rtc_data;      /**< Persists across deep sleep */
static BLEAdvertising* pAdvertising = nullptr; /**< BLE advertising handle */



/* ============= Function Prototypes ============= */
/* Core State Functions */
static bool initializeHardware(void);
static void enterFactoryMode(void);
static void enterNormalMode(void);
static void handleError(const ErrorCode& error);

/* Hardware Control */
static void powerDownDomains(void);
static void disableUnusedPins(void);
static bool setupDeepSleepWakeup(const gpio_num_t wakeup_pin);

/* Security Functions */
static uint32_t generateSeed(void);
static uint32_t generateRollingCode(const uint32_t timestamp);

/* BLE Functions */
static bool setupBLE(void);
static void broadcastBeacon(void);

/* Utility Functions */
static void printDebugInfo(uint32_t code);
static String getMacAddressEx(bool raw = false, uint8_t* mac_out = nullptr);
static String getMacAddress(void);
static void optimizeClocks(void);




/**
 * @brief Arduino setup function
 */
void setup() {
  DEBUG_INIT();
  DEBUG_VERBOSE(DBG_INIT);

  // Validate RTC memory initialization
  if (rtc_data.magic != RTC_DATA_MAGIC) {
    // First-time or corrupted RTC memory
    DEBUG_VERBOSE("\n[RTC] Memory validation failed - initializing");
    memset(&rtc_data, 0, sizeof(rtc_data));
    rtc_data.magic = RTC_DATA_MAGIC;
    rtc_data.state = DeviceState::UNINITIALIZED;
    rtc_data.is_initialized = false;
    rtc_data.lastError = ErrorCode::NONE;
  }

  // Initialize hardware
  if (!initializeHardware()) {
    handleError(rtc_data.lastError);
  }

  // Determine operation mode
  // -- OLD
  // if (!rtc_data.is_initialized || (esp_reset_reason() == ESP_RST_POWERON && digitalRead(BOOT_PIN) == LOW)) {
  // -- NEW
  if (!rtc_data.is_initialized || (esp_reset_reason() == ESP_RST_POWERON && gpio_get_level(BOOT_PIN) == 0)) {
    rtc_data.state = DeviceState::FACTORY_MODE;
    DEBUG_VERBOSE(DBG_FACTORY_WARN);
    enterFactoryMode();
  } else {
    rtc_data.state = DeviceState::NORMAL_MODE;
    enterNormalMode();
  }
}


/**
 * @brief Arduino Main loop function
 * @note  Unused but device sleeps between operations
 */
void loop() {
  delay(1000);  // Required for ESP32 background tasks
}


/**
* @brief Configure deep sleep wakeup on specified GPIO using EXT1 (ESP32-H2)
* @param wakeup_pin RTC-capable GPIO to use as wakeup source (GPIOs 0-10)
* @return bool true if wakeup configured successfully, false on any error
*/
static bool setupDeepSleepWakeup(const gpio_num_t wakeup_pin) {
  DEBUG_VERBOSE("\n[DEEP SLEEP] Configuring wakeup...");
  // Create bitmask for the provided pin
  const uint64_t ext_wakeup_pin_1_mask = 1ULL << wakeup_pin;
  // Configure EXT1 wakeup
  esp_err_t result = esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_LOW);

  switch (result) {
    case ESP_OK:
      DEBUG_VERBOSE_F("\n[DEEP SLEEP] Wakeup configuration successful for GPIO%d", wakeup_pin);
      return true;
    case ESP_ERR_INVALID_ARG:
      DEBUG_VERBOSE_F("\n[ERROR] Invalid argument - GPIO%d might not be RTC capable", wakeup_pin);
      return false;
    case ESP_ERR_NOT_ALLOWED:
      DEBUG_VERBOSE_F("\n[ERROR] Operation not allowed for GPIO%d", wakeup_pin);
      return false;
    default:
      DEBUG_VERBOSE_F("\n[ERROR] Unknown error: %d for GPIO%d", result, wakeup_pin);
      return false;
  }
}


/**
 * @brief Optimize power consumption by disabling unused peripherals and configuring clocks
 * @details Disables various ESP32-H2 peripherals and their associated GPIOs:
 *          1. Basic Peripherals:
 *             - LEDC: LED PWM (GPIOs 2-5) ~0.3mA
 *             - MCPWM0: Motor Control (GPIOs 15-20) ~0.5mA
 *             - PCNT: Pulse Counter (GPIOs 10-13) ~0.1mA
 *          
 *          2. Additional Peripherals:
 *             - RMT: Remote Control (GPIOs 0,1,2) ~0.2mA
 *             - SARADC: ADC Controller (GPIO 0-6 analog channels) ~0.5mA
 *             - SYSTIMER: No dedicated GPIOs, internal timer ~0.1mA
 *             - UART1: Secondary UART (GPIO 10-TX, 11-RX) ~0.3mA
 *             - SPI2: SPI Interface (MISO-12, MOSI-13, CLK-14, CS-15) ~0.4mA
 *             - I2C0: I2C Interface (SDA-16, SCL-17) ~0.2mA
 * 
 * @note 1. CPU frequency is locked at 96MHz when BLE is active
 * @note 2. UART0 should not be disabled if using Serial debug
 * @note 3. BT module should not be disabled if quick BLE restart needed
 * @note 4. Can't disable TIMG0/1: Timer Groups as they are used for RTC and other things ...
 */
static void optimizeClocks(void) {
  DEBUG_VERBOSE("\n[POWER] ----------------------");
  DEBUG_VERBOSE("\n[POWER] Starting peripheral disable...");

  uint32_t initial_freq = getCpuFrequencyMhz();
  DEBUG_VERBOSE_F("\n[POWER] Initial CPU Frequency: %d MHz", initial_freq);

  // Set CPU frequency to minimum required
  setCpuFrequencyMhz(80);
  // - Lower frequency = lower power consumption: ~50% power reduction compared to 160MHz (Default usually)
  // - Still maintains BLE functionality at 80MHz. Going below 80MHz would make BLE unstable
  // - Check if frequency set correctly
  uint32_t new_freq = getCpuFrequencyMhz();
  // -- TBT
  // ** Note: The ESP32-H2 seems to be locked at 96MHz for BLE operation
  if (new_freq != 80) {
    DEBUG_VERBOSE_F("\n[POWER] WARNING: CPU Frequency set to %d MHz instead of 80 MHz", new_freq);
  } else {
    DEBUG_VERBOSE("\n[POWER] CPU Frequency set successfully to 80 MHz");
  }

// -- TBT
// ** Note: Configure XTAL frequency for BLE but clock functions are not directly accessible in Arduino framework
// ** Note: ESP32-H2 uses different clock configuration compared to other ESP32s
// rtc_xtal_freq_t current_freq = rtc_clk_xtal_freq_get();
// if (current_freq != RTC_XTAL_FREQ_32M) {
//   rtc_clk_xtal_freq_set(RTC_XTAL_FREQ_32M);
// }


// For ESP32-H2, use correct module definitions
#ifdef CONFIG_IDF_TARGET_ESP32H2
  periph_module_disable(PERIPH_LEDC_MODULE);    // Disable unused peripherals (LED PWM module)
  periph_module_disable(PERIPH_MCPWM0_MODULE);  // Disable unused peripherals (Motor Control PWM )
  periph_module_disable(PERIPH_PCNT_MODULE);    // Disable unused peripherals (Pulse Counter)

// Additional peripherals that can be disabled
#if DEBUG_LED == DEBUG_LED_DISABLED
  periph_module_disable(PERIPH_RMT_MODULE);  // Remote Control
#endif
  // ** Disabling it Affects WS2812B/NeoPixel LED control and other PWM functions,
  // precise timing-based signals, IR remote control decoding
  // & other remote control protocols.
  // So, to save more current, if debug LED is disabled (i.e. our WS2812B/NeoPixel LED),
  // we can enable the disablement of RMT CTRL PERIF

  periph_module_disable(PERIPH_SARADC_MODULE);    // ADC
  periph_module_disable(PERIPH_SYSTIMER_MODULE);  // System Timer
  // -- NEW -- //
  esp_timer_early_init();  // InitESP timer early with minimal config, needed for stat LED blinks
  // --------- //
  periph_module_disable(PERIPH_UART1_MODULE);  // UART1
  periph_module_disable(PERIPH_SPI2_MODULE);   // SPI2
  periph_module_disable(PERIPH_I2C0_MODULE);   // I2C0

  periph_module_disable(PERIPH_RSA_MODULE);  // Cryptographic Module
#endif


  DEBUG_VERBOSE("\n[POWER] Peripherals disabled: LEDC, MCPWM0, PCNT, RMT, SARADC, TIMERS, UART1, SPI2, I2C0");
  // TBD - DEBUG_VERBOSE("\n[POWER] XTAL frequency configured for BLE");
  DEBUG_VERBOSE("\n[POWER] EST. total power savings: ~3.0mA");
  DEBUG_VERBOSE("\n[POWER] ----------------------");
}


/**
 * @brief Disables unused GPIO pins to reduce power consumption
 * @details Configures specified pins as outputs, pulls them low and enables pin hold
 *          during sleep modes to prevent floating states
 */
static void disableUnusedPins(void) {
  // Array of GPIO pins to be disabled
  const gpio_num_t unusedPins[] = {
    // GPIO_NUM_1,  // This is TX pin (Serial/UART TX): we are pulling them low in debug_log.h, if serial is not used
    GPIO_NUM_2,  // No connection on devkit
    // GPIO_NUM_3,  // This is RX pin (Serial/UART RX): we are pulling them low in debug_log.h, if serial is not used

    // -- OPTIONAL - can be disabled if JTAG debug not needed -- //
    GPIO_NUM_4,  // JTAG pins TMS
    GPIO_NUM_5,  // JTAG pins TDI
    GPIO_NUM_6,  // JTAG pins TCK
    GPIO_NUM_7,  // JTAG pins TDO
    // --------------------------------------------------------- //
    GPIO_NUM_10,  // No critical function
    GPIO_NUM_11,  // No critical function
    GPIO_NUM_12,  // No critical function
    GPIO_NUM_13   // No critical function

    // ** Note: DO NOT DISABLE - System critical GPIOs (GPIO_NUM_14 - GPIO_NUM_21), reserved for internal functions
    // These pins are connected to:
    // - Internal flash interface
    // - System memory interface
    // - Core functionality
    // Pulling these low causes system instability and resets
  };

  for (gpio_num_t pin : unusedPins) {
    // Initialize GPIO configuration structure
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;        // Disable interrupts
    io_conf.mode = GPIO_MODE_OUTPUT;              // Set as output
    io_conf.pin_bit_mask = (1ULL << pin);         // Create pin mask
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // Enable pulldown
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // Disable pullup

    gpio_config(&io_conf);   // Apply configuration
    gpio_set_level(pin, 0);  // Set pin low
    gpio_hold_en(pin);       // Hold pin state during sleep
    // TBD
    // rtc_gpio_isolate(pin);   // Isolate GPIO during deep sleep
  }
}


/**
 * @brief Hardware initialization
 * @return bool true if all initializations successful, false otherwise
 */
static bool initializeHardware(void) {
  bool success = true;

  DEBUG_VERBOSE(DBG_HW_INIT);
  DEBUG_VERBOSE_F(DBG_HW_STATE, static_cast<int>(rtc_data.state));

  // Configure status LED
  LED_INIT();

  // Add clock optimization here - before BLE init but after basic setup
  optimizeClocks();

  // -- OLD
  // Configure BOOT button with internal pullup
  // pinMode(BOOT_PIN, INPUT_PULLUP);
  // -- NEW
  // Native ESP-IDF configuration for input with pull-up
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << BOOT_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
    .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE
  };
  gpio_config(&io_conf);

  // Disable unused pins
  disableUnusedPins();

  // ** IMPORTANT: Always try to setup BLE, regardless of state
  if (!setupBLE()) {
    DEBUG_VERBOSE(DBG_ERR_BLE);
    success = false;
    rtc_data.lastError = ErrorCode::BLE_INIT_FAILED;
  }

  DEBUG_VERBOSE_F(DBG_HW_RESULT, success ? "SUCCESS" : "FAILED");
  return success;
}


/**
* @brief Initializes BLE advertising functionality
* @details Setup sequence:
* 1. Initialize BLE device with product name
* 2. Get advertising handle
* 3. Set minimum TX power for 2m range
* 4. Configure advertising parameters:
*    - Disable scan response
*    - Set intervals (0x20 to 0x40 slots)
* 
* @return bool Initialization status
*    - true: BLE initialized successfully
*    - false: Failed to get advertising handle
* 
* @note Advertising intervals:
*    - Min: 0x20 * 0.625ms = 20ms
*    - Max: 0x40 * 0.625ms = 40ms
*/
static bool setupBLE(void) {
  DEBUG_VERBOSE(DBG_BLE_INIT);

  // Deinitialize BLE first to ensure clean state
  BLEDevice::deinit(true);

  // ** Get default config
  // esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  // ** Keep the defaults for now as they're optimized for low power
  // *** Note: TBD Maybe revisit

  try {
    BLEDevice::init(PRODUCT_NAME);

    // TBT
    // -- OLD
    // // Set minimum TX power for 2m range
    // esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);  // -12dBm
    // -- NEW
    // Set minimum transmit power for advertising and scanning
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);   // -12dBm
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_N12);  // -12dBm

    pAdvertising = BLEDevice::getAdvertising();

    if (pAdvertising == nullptr) {
      DEBUG_VERBOSE(DBG_ERR_BLE_NULL);
      return false;
    }

    // Optimize advertising parameters for power saving
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinInterval(0x40);  // 40ms * 0.625ms = 25ms
    pAdvertising->setMaxInterval(0x80);  // 80ms * 0.625ms = 50ms

    DEBUG_VERBOSE(DBG_BLE_SETUP);
    return true;
  } catch (std::exception& e) {
    DEBUG_VERBOSE_F(DBG_ERR_BLE_EXCEPT, e.what());
    return false;
  }
}




/**
 * @brief Get device MAC address
 * @return String MAC address in XX:XX:XX:XX:XX:XX format
 */
// Uses custom MAC burned to efuses by: espefuse.py --chip esp32h2 --port /dev/cu.usbserial-2120 burn_custom_mac <MAC address>
// Modified to allow getting either MAC string or raw bytes
static String getMacAddressEx(bool raw, uint8_t* mac_out) {
  uint8_t mac[6];
  size_t mac_size = 6;
  esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_CUSTOM_MAC, mac, mac_size * 8);

  // If raw bytes are requested, copy to output buffer
  if (raw && mac_out != nullptr && err == ESP_OK) {
    memcpy(mac_out, mac, 6);
  }

  // Format string representation
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  return String(mac_str);
}

static String getMacAddress(void) {
  return getMacAddressEx(false, nullptr);
}




/**
 * @brief Generate unique device seed
 * @return uint32_t Unique seed value
 */
static uint32_t generateSeed(void) {
  uint8_t macAddr[6];
  getMacAddressEx(true, macAddr);  // Get raw custom unique MAC bytes using our new function

  uint32_t seed = PRODUCT_KEY;
  seed ^= ((uint32_t)BATCH_ID << 16);
  seed ^= ((macAddr[0] << 24) | (macAddr[1] << 16) | (macAddr[2] << 8) | macAddr[3]);
  return seed;
}




/**
* @brief Handles factory reset mode and initialization
* @details Operation sequence:
* 1. Initialization:
*    - Red LED indication
*    - Generate new device seed
*    - Reset counter
* 2. Device info display:
*    - MAC address
*    - Generated seed
*    - Operation instructions
* 3. Wait for user action:
*    - 20 second timeout (FACTORY_WAIT_MS)
*    - Early exit on BOOT button press
* 4. Transition:
*    - Mark device as initialized
*    - Update state to NORMAL_MODE
*    - Enter normal operation
*
* @note Factory mode is entered on first boot or uninitialized state
*/
static void enterFactoryMode(void) {
  // LED Status: Factory Mode - Yellow
  // LED_YELLOW();
  LED_INIT();  // Make sure LED is initialized

  DEBUG_VERBOSE(DBG_FACTORY_ENTER);

  // Generate and store new seed
  rtc_data.seed = generateSeed();
  rtc_data.counter = 0;

  // Print device information
  DEBUG_VERBOSE_F(DBG_MAC_CUSTOM, getMacAddress().c_str());  // unique custm set MAC address
  DEBUG_VERBOSE_F(DBG_FACTORY_SEED, rtc_data.seed);
  DEBUG_VERBOSE(DBG_FACTORY_WAIT);  // msg: "Will await 20 sec to jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation."

  // Wait for button press or timeout
  uint32_t start_time = millis();
  while (millis() - start_time < FACTORY_WAIT_MS) {
    BLINK_YELLOW_LED(250);  // Call the blink function frequently
    // -- OLD
    // if (digitalRead(BOOT_PIN) == LOW) {
    // -- NEW
    if (gpio_get_level(BOOT_PIN) == 0) {
      delay(10);  // Shorter debounce
      break;
    }
    delay(1);  // Very short delay to allow other tasks
  }

  // Transition to normal operation (steps)
  rtc_data.is_initialized = true;
  rtc_data.state = DeviceState::NORMAL_MODE;

  DEBUG_VERBOSE(DBG_FACTORY_TRANS);
  DEBUG_FLUSH();  // Allow serial to flush
  // LED_OFF();      // Turn off LEDs

  enterNormalMode();
}




/**
* @brief Generates secure rolling code using seed, timestamp (used for generating rolling code), and mixing operations
* @details Algorithm flow:
* 1. Gets 32-bit timestamp from esp_timer
* 2. Combines with stored seed via multi-stage mixing:
*    - Initial mix: (seed ^ timestamp) * prime1
*    - Stage 1: XOR with right-shifted (13 bits)
*    - Stage 2: Multiply by prime2 (0x5C4D)
*    - Stage 3: XOR with right-shifted (17 bits)
*    - Stage 4: Multiply by seed again
*    - Final: XOR with right-shifted (16 bits)
* 
* @return uint32_t Generated rolling code
* @note Uses prime multipliers and bit shifts for avalanche effect
*/
static uint32_t generateRollingCode(const uint32_t timestamp) {
  uint32_t base = rtc_data.seed;

  uint32_t mixed = (base ^ timestamp) * 0x7FFF;  // Prime1 multiplication
  mixed = mixed ^ (mixed >> 13);                 // First diffusion
  mixed = mixed * 0x5C4D;                        // Prime2 multiplication
  mixed = mixed ^ (mixed >> 17);                 // Second diffusion
  mixed = mixed * rtc_data.seed;                 // Additional mixing
  mixed = mixed ^ (mixed >> 16);                 // Final diffusion

  return mixed;
}



/**
* @brief Handles normal operation mode of the device
* @details Operation sequence:
* 1. Status indication:
*    - Green LED
*    - Serial logging
* 2. Main operations:
*    - Generate rolling code
*    - (Optional)Print debug info
*    - Broadcast over BLE
* 3. Sleep preparation:
*    - Increment counter
*    - Configure BOOT_PIN as wakeup source
*    - Turn off LED
*    - Enter deep sleep
*
* @note Device wakes on BOOT_PIN low signal
*/
static void enterNormalMode(void) {
  // LED Status: Active/Normal - Green
  LED_GREEN();

  DEBUG_VERBOSE(DBG_NORMAL_ENTER);

  // Core operations:
  // 1. Broadcast Rolling code
  // 2. Go to Sleep

  // 1. Broadcast Rolling code
  // Single call to broadcast - it handles timestamp,
  // rolling code generation and broadcasting internally
  broadcastBeacon();

  rtc_data.counter++;

  // 3. Prep to sleep ...
  DEBUG_VERBOSE(DBG_NORMAL_SLEEP);

  // Configure wakeup on GPIO ...
  // Ext1 Wakeup (Multiple Pins) (also, only one for ESP32-H2): Can wake up on MULTIPLE GPIO pins simultaneously & offers more complex
  // setupDeepSleepWakeup();
  if (!setupDeepSleepWakeup(BOOT_PIN)) {
    DEBUG_VERBOSE("\n[ERROR] Deep sleep wakeup configuration failed ❌");
    DEBUG_VERBOSE("\n[ERROR] So, will not go to sleep (exiting function ...) 😳\n");
    return;
  }
  DEBUG_VERBOSE("\n[WARNING] Will go to sleep as we could setup wakeup pin. 🥱\n");

  DEBUG_FLUSH();   // Allow serial to flush
  DEBUG_DEINIT();  // Kill Serial / Deinitilaize Serial
  LED_OFF();       // Turn off LEDs

  // -- TBT Disable Neopixel LED pin, maybe ??

  // -- TBT
  // Problem: - The system is restarting instead of properly waking from deep sleep when these power domains are configured
  // Solution: - No explicit power domain configuration. Let ESP-IDF handle the power domains automatically for stable wake-up
  powerDownDomains();

  // Go to sleep
  esp_deep_sleep_start();
}


/**
* @brief Broadcasts rolling code via BLE advertising
* @param code 32-bit rolling code to broadcast
* @details Packet structure [12 bytes total]:
*   - Header: MANUFACTURER_ID [2B]
*   - Type: Rolling code identifier [1B]
*   - Length: Payload length [1B]
*   - Payload: Rolling code [4B]
*   - Payload: Timestamp [4B]      // NEW
*   - CRC: Checksum [1B]
* @flow:
* 1. Validates BLE initialization
* 2. Splits 32-bit code into 4 bytes
* 3. Splits 32-bit timestamp into 4 bytes  // NEW
* 4. Creates BLE advertisement payload
* 5. Broadcasts for BEACON_TIME_MS duration
*
* @note Total payload increased from 9 to 12 bytes to accommodate timestamp
*       This aids web-app verification by providing timing context
*/
static void broadcastBeacon() {
  if (!pAdvertising) {
    DEBUG_VERBOSE(DBG_ERR_BLE_UNINIT);
    return;
  }

  // Get timestamp ONCE for both operations
  uint32_t timestamp = esp_timer_get_time() & 0xFFFFFFFF;

  // Generate rolling code using this timestamp
  uint32_t code = generateRollingCode(timestamp);

  printDebugInfo(code);  // Add this here, using same generated code

  // Create 8-byte payload
  uint8_t payload[8];
  // Rolling code (first 4 bytes)
  payload[0] = (code >> 24) & 0xFF;
  payload[1] = (code >> 16) & 0xFF;
  payload[2] = (code >> 8) & 0xFF;
  payload[3] = code & 0xFF;
  // Same timestamp used for generation (next 4 bytes)
  payload[4] = (timestamp >> 24) & 0xFF;
  payload[5] = (timestamp >> 16) & 0xFF;
  payload[6] = (timestamp >> 8) & 0xFF;
  payload[7] = timestamp & 0xFF;

  // Create advertisement data first
  BLEAdvertisementData advData;
  advData.setName(PRODUCT_NAME);
  String data;
  for (int i = 0; i < 8; i++) {
    data += (char)payload[i];
  }
  advData.setManufacturerData(data);

  // Now debug output with the created data
  DEBUG_VERBOSE("\n[BLE] Complete Advertisement Packet Structure:");
  DEBUG_VERBOSE_F("\n      Header: Manufacturer ID [2B]: 0x%04X", MANUFACTURER_ID);
  DEBUG_VERBOSE("\n      Type: Rolling code identifier [1B]");
  DEBUG_VERBOSE_F("\n      Length: Payload length [1B]: %d", 8);  // 8 bytes payload
  DEBUG_VERBOSE("\n      Payload [8B]:");
  DEBUG_VERBOSE("\n          Rolling Code [4B]:");
  for (int i = 0; i < 4; i++) {
    DEBUG_VERBOSE_F("\n          [%d]: 0x%02X", i, payload[i]);
  }
  DEBUG_VERBOSE("\n          Timestamp [4B]:");
  DEBUG_VERBOSE_F("\n          Full Value: 0x%08X", timestamp);
  for (int i = 4; i < 8; i++) {
    DEBUG_VERBOSE_F("\n          [%d]: 0x%02X", i, payload[i]);
  }
  DEBUG_VERBOSE("\n[BLE] Complete Adv Packet:");
  DEBUG_VERBOSE_F("\n      Name: %s", PRODUCT_NAME);
  DEBUG_VERBOSE("\n      Data: ");
  for (int i = 0; i < data.length(); i++) {
    DEBUG_VERBOSE_F("0x%02X ", (uint8_t)data[i]);
  }
  DEBUG_VERBOSE_F("\n      Total Packet Size: %d bytes", 12);  // 2+1+1+8 bytes
  DEBUG_VERBOSE("\n");

  pAdvertising->setAdvertisementData(advData);

  // Start advertising for specified duration
  DEBUG_VERBOSE_F(DBG_BLE_BROADCAST_WARN, static_cast<int>(BEACON_TIME_MS / 1000));
  pAdvertising->start();
  delay(BEACON_TIME_MS);
  pAdvertising->stop();
}




/**
 * @brief Power down domains - ESP32-H2 specific - handles how esp32 wakes up from sleep - wake up of int stacks sequece
 * @note [TBD] The system is restarting instead of properly waking from deep sleep when these power domains are configured 
*/
static void powerDownDomains(void) {
  // ** TBT: these are err_states . try checking why they are causing errors?

  // Power down domains - ESP32-H2 specific
  // -- Core Clock Domains -- //
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST, ESP_PD_OPTION_OFF);  // ..._AUTO, ..._OFF
  // Fast RC Oscillator (20MHz)
  // - Used for quick CPU startup
  // - Turning OFF saves power but increases wake-up time
  // - Less accurate than XTAL

  // esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL32K, ESP_PD_OPTION_OFF);
  // External 32KHz Crystal
  // - Used for real-time keeping
  // - Low power, high accuracy timing
  // - Important for timed wake-ups
  // - Not available on ESP32-H2 (will cause compile error)

  // esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);  // ..._AUTO, ..._OFF
  // Main Crystal Oscillator (32MHz)
  // - Primary high-precision clock
  // - Used for BLE and precise timing
  // - Turning OFF saves significant power but increases wake-up time
  // - Required for BLE operation

  // esp_sleep_pd_config(ESP_PD_DOMAIN_RC32K, ESP_PD_OPTION_OFF);
  // Internal 32KHz RC Oscillator
  // - Backup low-power clock
  // - Less accurate than XTAL32K
  // - Not available on ESP32-H2 (will cause compile error)

  // -- Function Domains -- //
  // esp_sleep_pd_config(ESP_PD_DOMAIN_CPU, ESP_PD_OPTION_OFF);
  // CPU Core
  // - Main processor
  // - Must be OFF during deep sleep
  // - Automatically managed by sleep functions

  // -- TBT / Maybe not
  // esp_sleep_pd_config(ESP_PD_DOMAIN_BT, ESP_PD_OPTION_OFF);
  // Bluetooth Subsystem
  // - Controls BLE radio
  // - Keep ON if you need fast BLE startup
  // - Significant power impact
  // - ** Don't disable BT domain if you need quick BLE startup
}



/**
* @brief Handles system errors and provides visual feedback
* @details 
* - Updates device state to ERROR
* - Logs error code to serial
* - Provides visual error indication via LED:
*   - 5 red blinks at 1Hz (500ms on/off)
* 
* @param error ErrorCode enum indicating type of error
* @note Returns immediately if error is NONE
*/
static void handleError(const ErrorCode& error) {
  if (error == ErrorCode::NONE) {
    return;
  }

  rtc_data.state = DeviceState::ERROR;

  // Detailed error logging
  switch (error) {
    case ErrorCode::BLE_INIT_FAILED:
      DEBUG_VERBOSE(DBG_CRIT_BLE);
      DEBUG_VERBOSE_F("[DEBUG] Advertising Pointer: %p\n", pAdvertising);
      break;
    case ErrorCode::INVALID_STATE:
      DEBUG_VERBOSE(DBG_CRIT_STATE);
      break;
    default:
      DEBUG_VERBOSE(DBG_CRIT_UNKNOWN);
      break;
  }

  // Error indication - Red blink
  for (int i = 0; i < 5; i++) {
    LED_RED();
    delay(100);
    LED_OFF();
    delay(100);
  }

  DEBUG_FLUSH();

  // ** Optional: Soft reset
  ESP.restart();
}



/**
 * @brief Print debug information to serial
 * @param code Current rolling code value
 */
static void printDebugInfo(uint32_t code) {
  DEBUG_VERBOSE(DBG_DEBUG_START);
  // Print IEEE802154 MAC that is seen by BLE apps (used for defualt BLE adv header and unique but same to all radios from one manufacturers)
  uint8_t ble_mac[6];
  esp_read_mac(ble_mac, ESP_MAC_IEEE802154);
  char ble_mac_str[18];
  snprintf(ble_mac_str, sizeof(ble_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           ble_mac[0], ble_mac[1], ble_mac[2], ble_mac[3], ble_mac[4], ble_mac[5]);
  // Print the unique, custom set MAC address (that we set by eFuse settings and used in rolling code)
  DEBUG_VERBOSE_F(DBG_DEBUG_MAC, getMacAddress().c_str());
  DEBUG_VERBOSE_F(DBG_DEBUG_MAC_BLE, ble_mac_str);
  DEBUG_VERBOSE_F(DBG_DEBUG_KEY, PRODUCT_KEY);
  DEBUG_VERBOSE_F(DBG_DEBUG_BATCH, BATCH_ID);
  DEBUG_VERBOSE_F(DBG_DEBUG_SEED, rtc_data.seed);
  DEBUG_VERBOSE_F(DBG_DEBUG_COUNTER, rtc_data.counter);
  DEBUG_VERBOSE_F(DBG_DEBUG_ROLLING_CODE, code);
  DEBUG_VERBOSE(DBG_DEBUG_ALGO);
  DEBUG_VERBOSE(DBG_DEBUG_END);
}
