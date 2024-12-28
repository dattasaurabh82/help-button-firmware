/**
 * @file    secure_ble_beacon.ino
 * @brief   Secure BLE Emergency Beacon Implementation for ESP32-H2
 * @details Implements secure rolling code beacon with factory reset and sleep modes
 * 
 * @author  Saurabh Datta | Datta+Baum Studio
 * @date    2024-12
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

#include "config.h"  // Imports for shared constants for rolling code generation
#include "debug.h"   // Debug macros
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include "esp_mac.h"
#include <Adafruit_NeoPixel.h>

/**
* The BLE config defines can be removed since they're in sdkconfig.h
* So, if furthr optimization needed, do it there. 
 */
// #define CONFIG_BT_BLE_50_FEATURES_SUPPORTED 0
// #define CONFIG_BT_BLE_42_FEATURES_SUPPORTED 1
// #define CONFIG_BT_CTRL_MODE_BR_EDR_ONLY 0
// #define CONFIG_BT_CTRL_MODE_EFF 1


/* ========= string literals to PROGMEM (Used specifically in debug messages)  ========= */
static const char PROGMEM MSG_INIT[] = "\n[INIT] Starting Emergency Beacon...";
static const char PROGMEM MSG_FACTORY_WARN[] = "[WARNING] Factory reset required";
static const char PROGMEM MSG_FACTORY_ENTER[] = "\n[FACTORY] Entering Factory Reset Mode";
static const char PROGMEM MSG_FACTORY_MAC[] = "[FACTORY] Device MAC: %s\n";
static const char PROGMEM MSG_FACTORY_SEED[] = "[FACTORY] Generated Seed: 0x%08lX\n";
static const char PROGMEM MSG_FACTORY_WAIT[] = "[FACTORY] Will await 60 sec to jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation.\n";
static const char PROGMEM MSG_FACTORY_BUTTON[] = "[FACTORY] Button press detected";
static const char PROGMEM MSG_FACTORY_TRANSITION[] = "[FACTORY] Transitioning to Normal Mode";
static const char PROGMEM MSG_NORMAL_ENTER[] = "\n[NORMAL] Entering Normal Operation Mode";
static const char PROGMEM MSG_NORMAL_SLEEP[] = "[NORMAL] Entering deep sleep";
static const char PROGMEM MSG_ERROR[] = "[ERROR] Code: %d\n";
static const char PROGMEM MSG_DEBUG_START[] = "\n=== Debug Information ===";
static const char PROGMEM MSG_DEBUG_MAC[] = "MAC Address: %s\n";
static const char PROGMEM MSG_DEBUG_KEY[] = "Product Key: 0x%08lX\n";
static const char PROGMEM MSG_DEBUG_BATCH[] = "Batch ID: 0x%04X\n";
static const char PROGMEM MSG_DEBUG_SEED[] = "Current Seed: 0x%08lX\n";
static const char PROGMEM MSG_DEBUG_COUNTER[] = "Counter: %lu\n";
static const char PROGMEM MSG_DEBUG_CODE[] = "Rolling Code: 0x%08lX\n";
static const char PROGMEM MSG_DEBUG_ALGO[] = "Algorithm: Mixed-bit with time seed\n";
static const char PROGMEM MSG_DEBUG_END[] = "=======================\n";


/* ============= Configuration Constants ============= */
#define PRODUCT_NAME "HELP by JENNYFER" /**< Product-specific name */
/* ========== NOTE ========== */
// * Moved to Macros (config.h)
// #define PRODUCT_KEY 0xXXX   /**< Product-specific key */
// #define BATCH_ID 0xXXX      /**< Production batch identifier */
// * Create a file called config.h by following the config_template.h and for the values themselves, contact the developer :)
/* ========================== */
#define BOOT_PIN 9               /**< GPIO pin for BOOT button */
#define BEACON_TIME_MS 10000     /**< Broadcast duration in ms */
#define FACTORY_WAIT_MS 60000    /**< Factory reset timeout in ms */
#define STATUS_LED_PIN 8         /**< GPIO pin for NeoPixel LED */
#define STATUS_LED_BRIGHTNESS 50 /**< LED brightness (0-255) */





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
  LED_INIT_FAILED,
  INVALID_STATE
};

/**
 * @brief RTC data structure
 * @details Data persisted in RTC memory across deep sleep cycles
 */
typedef struct __attribute__((packed)) {
  uint32_t seed;       /**< Device-specific seed */
  uint32_t counter;    /**< Rolling code counter */
  bool is_initialized; /**< Initialization flag */
  DeviceState state;   /**< Current device state */
  ErrorCode lastError; /**< Last error encountered */
} rtc_data_t;





/* ============= Global Variables ============= */
RTC_DATA_ATTR static rtc_data_t rtc_data;                              /**< Persists across deep sleep */
static BLEAdvertising* pAdvertising = nullptr;                         /**< BLE advertising handle */
static Adafruit_NeoPixel led(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800); /**< Status LED */





/* ============= Function Prototypes ============= */
/* Core State Functions */
static bool initializeHardware(void);
static void enterFactoryMode(void);
static void enterNormalMode(void);
// ** TBT
// static void handleError(ErrorCode error);
static void handleError(const ErrorCode& error);

/* Hardware Control */
static bool setupLed(void);
// ** TBT
// static void setLedColor(uint8_t r, uint8_t g, uint8_t b);
static void setLedColor(const uint8_t& r, const uint8_t& g, const uint8_t& b);
static bool setupBLE(void);
static void disableUnusedPins(void);

/* Security Functions */
static uint32_t generateSeed(void);
static uint32_t generateRollingCode(void);
// ** TBT
// static void broadcastBeacon(uint32_t code);
static void broadcastBeacon(const uint32_t& code);

/* Utility Functions */
// ** TBT
// static void printDebugInfo(uint32_t code);
static void printDebugInfo(const uint32_t& code);
static String getMacAddress(void);



/**
 * @brief Hardware initialization
 * @return bool true if all initializations successful, false otherwise
 */
static bool initializeHardware(void) {
  bool success = true;

  // Configure status LED
  if (!setupLed()) {
    success = false;
    rtc_data.lastError = ErrorCode::LED_INIT_FAILED;
  }

  // Configure BOOT button with internal pullup
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // Disable unused pins
  disableUnusedPins();

  // Initialize BLE if in normal mode
  if (rtc_data.state == DeviceState::NORMAL_MODE) {
    if (!setupBLE()) {
      success = false;
      rtc_data.lastError = ErrorCode::BLE_INIT_FAILED;
    }
  }

  return success;
}



/**
 * @brief Arduino setup function
 */
void setup() {
  // Serial.begin(115200);
  DEBUG_BEGIN(115200);
  // Serial.print(FPSTR(MSG_INIT));
  DEBUG_LOG(FPSTR(MSG_INIT));

  // Initialize hardware
  if (!initializeHardware()) {
    handleError(rtc_data.lastError);
    return;
  }

  // Determine operation mode
  if (!rtc_data.is_initialized || (esp_reset_reason() == ESP_RST_POWERON && digitalRead(BOOT_PIN) == LOW)) {
    rtc_data.state = DeviceState::FACTORY_MODE;
    // Serial.println(F("[WARNING] Factory reset required"));
    // Serial.print(FPSTR(MSG_FACTORY_WARN));
    DEBUG_LOG(FPSTR(MSG_FACTORY_WARN));
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
 * @brief LED initialization
 * @return bool true if LED initialized successfully
 */
static bool setupLed(void) {
  led.begin();
  led.setBrightness(STATUS_LED_BRIGHTNESS);
  led.clear();
  led.show();
  return true;
}




/**
 * @brief Set LED color
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
static void setLedColor(const uint8_t& r, const uint8_t& g, const uint8_t& b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
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
  BLEDevice::init(PRODUCT_NAME);

  // Set minimum TX power for 2m range
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);  // -12dBm

  pAdvertising = BLEDevice::getAdvertising();
  if (!pAdvertising) {
    return false;
  }

  pAdvertising->setScanResponse(false);
  pAdvertising->setMinInterval(0x20);  // 20ms
  pAdvertising->setMaxInterval(0x40);  // 40ms
  return true;
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
*    - 60 second timeout (FACTORY_WAIT_MS)
*    - Early exit on BOOT button press
* 4. Transition:
*    - Mark device as initialized
*    - Update state to NORMAL_MODE
*    - Enter normal operation
*
* @note Factory mode is entered on first boot or uninitialized state
*/
static void enterFactoryMode(void) {
  // Serial.println(F("\n[FACTORY] Entering Factory Reset Mode"));
  // Serial.print(FPSTR(MSG_FACTORY_ENTER));
  DEBUG_LOG(FPSTR(MSG_FACTORY_ENTER));

  // LED Status: Factory Mode
  setLedColor(255, 0, 0);

  // Initialize device
  rtc_data.seed = generateSeed();
  rtc_data.counter = 0;

  // Display device info
  // Serial.printf("[FACTORY] Device MAC: %s\n", getMacAddress().c_str());
  // Serial.printf(FPSTR(MSG_FACTORY_MAC), getMacAddress().c_str());
  DEBUG_LOGF(FPSTR(MSG_FACTORY_MAC), getMacAddress().c_str());
  // Serial.printf("[FACTORY] Generated Seed: 0x%08lX\n", rtc_data.seed);
  // Serial.printf(FPSTR(MSG_FACTORY_SEED), rtc_data.seed);
  DEBUG_LOGF(FPSTR(MSG_FACTORY_SEED), rtc_data.seed);
  // Serial.printf("[FACTORY] Will await 60 sec to jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation.\n");
  // Serial.print(FPSTR(MSG_FACTORY_WAIT));
  DEBUG_LOGF(FPSTR(MSG_FACTORY_WAIT));

  // Wait for timeout or button press
  uint32_t start_time = millis();
  while (millis() - start_time < FACTORY_WAIT_MS) {
    if (digitalRead(BOOT_PIN) == LOW) {
      // Serial.println(F("[FACTORY] Button press detected"));
      // Serial.print(FPSTR(MSG_FACTORY_BUTTON));
      DEBUG_LOG(FPSTR(MSG_FACTORY_BUTTON));
      delay(100);  // Debounce delay
      break;
    }
    delay(100);  // Check interval
  }

  // Transition to normal operation
  rtc_data.is_initialized = true;
  rtc_data.state = DeviceState::NORMAL_MODE;
  // Serial.println(F("[FACTORY] Transitioning to Normal Mode"));
  // Serial.print(FPSTR(MSG_FACTORY_TRANSITION));
  DEBUG_LOG(FPSTR(MSG_FACTORY_TRANSITION));
  // Serial.flush();  // Ensure serial flush
  // delay(100);
  DEBUG_FLUSH();
  DEBUG_DELAY(100);
  enterNormalMode();
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
  // Serial.println(F("\n[NORMAL] Entering Normal Operation Mode"));
  // Serial.print(FPSTR(MSG_NORMAL_ENTER));
  DEBUG_LOG(FPSTR(MSG_NORMAL_ENTER));

  // LED Status: Active/Normal
  setLedColor(0, 255, 0);

  // Core operations
  uint32_t rolling_code = generateRollingCode();
  printDebugInfo(rolling_code);
  broadcastBeacon(rolling_code);

  // Prepare for sleep
  rtc_data.counter++;
  // Serial.println(F("[NORMAL] Entering deep sleep"));
  // Serial.print(FPSTR(MSG_NORMAL_SLEEP));
  // delay(100);  // Ensure serial buffer flushes
  DEBUG_LOG(FPSTR(MSG_NORMAL_SLEEP));
  DEBUG_DELAY(100);  // Ensure serial buffer flushes

  // Configure wakeup source
  const uint64_t ext_wakeup_pin_1_mask = 1ULL << BOOT_PIN;
  esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_LOW);

  setLedColor(0, 0, 0);  // Status: Sleeping
  esp_deep_sleep_start();
}




/**
 * @brief Disables unused GPIO pins to reduce power consumption
 * @details Configures specified pins as outputs, pulls them low and enables pin hold
 *          during sleep modes to prevent floating states
 */
static void disableUnusedPins(void) {
  // Array of GPIO pins to be disabled
  const gpio_num_t unusedPins[] = {
    GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5,
    GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_11, GPIO_NUM_12
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
  }
}




/**
* @brief Retrieves device's IEEE 802.15.4 MAC address as formatted string
* @details 
* - Reads 6-byte MAC using esp_read_mac()
* - Formats as uppercase hex with colons (XX:XX:XX:XX:XX:XX)
* - Uses snprintf for safe string formatting
* 
* @return String Formatted MAC address
*/
static String getMacAddress(void) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_IEEE802154);  // Get MAC address

  char mac_str[18];  // 17 chars for MAC + null terminator
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  return String(mac_str);
}




/**
* @brief Generates unique device seed from device MAC, product key and batch ID
* @details Combines:
*  - MAC address (4 bytes) - Hardware unique identifier
*  - Product key - Shared key for all devices (from config.h)
*  - Batch ID - Shared Production batch identifier (from config.h)
* Using XOR operations to generate seed:
*  1. Start with product key
*  2. XOR with shifted batch ID (upper 16 bits) 
*  3. XOR with first 4 MAC bytes (packed into 32 bits)
* 
* @return uint32_t Final 32-bit seed value
* @note Seed remains constant for device but unique across devices
*/
static uint32_t generateSeed(void) {
  uint8_t macAddr[6];
  esp_read_mac(macAddr, ESP_MAC_IEEE802154);  // Get IEEE 802.15.4 MAC address

  uint32_t seed = PRODUCT_KEY;         // Base seed with product key
  seed ^= ((uint32_t)BATCH_ID << 16);  // XOR with batch ID in upper 16 bits
  seed ^= ((macAddr[0] << 24) |        // XOR with first 4 MAC bytes
           (macAddr[1] << 16) | (macAddr[2] << 8) | macAddr[3]);
  return seed;
}




/**
* @brief Generates secure rolling code using seed, timestamp, and mixing operations
* @details Algorithm flow:
* 1. Gets 24-bit timestamp from esp_timer
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
static uint32_t generateRollingCode(void) {
  uint32_t timestamp = esp_timer_get_time() & 0xFFFFFF;  // 24-bit timestamp
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
 * @brief Broadcasts rolling code over BLE advertisement with custom formatting
 * @details Packet structure [9 bytes total]:
 *   - Header: MANUFACTURER_ID [2B]
 *   - Type: Rolling code identifier [1B]
 *   - Length: Payload length [1B]
 *   - Payload: Rolling code [4B]
 *   - CRC: Checksum [1B]
 * 
 * @param code 32-bit rolling code to broadcast
 */
static void broadcastBeacon(const uint32_t& code) {
  if (!pAdvertising) {
    handleError(ErrorCode::BLE_INIT_FAILED);
    return;
  }

  uint8_t packet[9];
  packet[0] = MANUFACTURER_ID & 0xFF;
  packet[1] = (MANUFACTURER_ID >> 8) & 0xFF;
  packet[2] = 0x01;
  packet[3] = 0x04;

  packet[4] = (code >> 24) & 0xFF;
  packet[5] = (code >> 16) & 0xFF;
  packet[6] = (code >> 8) & 0xFF;
  packet[7] = code & 0xFF;

  uint8_t crc = 0;
  for (int i = 0; i < 8; i++) {
    crc ^= packet[i];
  }
  packet[8] = crc;

  BLEAdvertisementData advData;
  advData.setName(PRODUCT_NAME);
  String data;
  for (int i = 0; i < sizeof(packet); i++) {
    data += (char)packet[i];
  }
  advData.setManufacturerData(data);
  pAdvertising->setAdvertisementData(advData);

  pAdvertising->start();
  delay(BEACON_TIME_MS);
  pAdvertising->stop();
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
  // Serial.printf("[ERROR] Code: %d\n", static_cast<int>(error));
  // Serial.printf(FPSTR(MSG_ERROR), static_cast<int>(error));
  DEBUG_LOGF(FPSTR(MSG_ERROR), static_cast<int>(error));

  // Visual error indicator - 5 red blinks
  for (int i = 0; i < 5; i++) {
    setLedColor(255, 0, 0);  // Red
    delay(500);              // On for 500ms
    setLedColor(0, 0, 0);    // Off
    delay(500);              // Off for 500ms
  }
}




/**
 * @brief Print debug information to serial
 * @param code Current rolling code value
 */
// static void printDebugInfo(const uint32_t& code) {
//   Serial.print(FPSTR(MSG_DEBUG_START));
//   Serial.printf(FPSTR(MSG_DEBUG_MAC), getMacAddress().c_str());
//   Serial.printf(FPSTR(MSG_DEBUG_KEY), PRODUCT_KEY);
//   Serial.printf(FPSTR(MSG_DEBUG_BATCH), BATCH_ID);
//   Serial.printf(FPSTR(MSG_DEBUG_SEED), rtc_data.seed);
//   Serial.printf(FPSTR(MSG_DEBUG_COUNTER), rtc_data.counter);
//   Serial.printf(FPSTR(MSG_DEBUG_CODE), code);
//   Serial.print(FPSTR(MSG_DEBUG_ALGO));
//   Serial.print(FPSTR(MSG_DEBUG_END));
// }
static void printDebugInfo(const uint32_t& code) {
  DEBUG_LOG(FPSTR(MSG_DEBUG_START));
  DEBUG_LOGF(FPSTR(MSG_DEBUG_MAC), getMacAddress().c_str());
  DEBUG_LOGF(FPSTR(MSG_DEBUG_KEY), PRODUCT_KEY);
  DEBUG_LOGF(FPSTR(MSG_DEBUG_BATCH), BATCH_ID);
  DEBUG_LOGF(FPSTR(MSG_DEBUG_SEED), rtc_data.seed);
  DEBUG_LOGF(FPSTR(MSG_DEBUG_COUNTER), rtc_data.counter);
  DEBUG_LOGF(FPSTR(MSG_DEBUG_CODE), code);
  DEBUG_LOG(FPSTR(MSG_DEBUG_ALGO));
  DEBUG_LOG(FPSTR(MSG_DEBUG_END));
}