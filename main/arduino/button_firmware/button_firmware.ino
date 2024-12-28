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


#include "config.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_mac.h>
#include <Adafruit_NeoPixel.h>




/* ============= Configuration Constants ============= */
#define PRODUCT_NAME "HELP by JENNYFER" /**< Product-specific name */
#define BOOT_PIN 9               /**< GPIO pin for BOOT button */
#define BEACON_TIME_MS 10000     /**< Broadcast duration in ms */
#define FACTORY_WAIT_MS 20000    /**< Factory reset timeout in ms */
#define STATUS_LED_PIN 8         /**< GPIO pin for NeoPixel LED */
#define STATUS_LED_BRIGHTNESS 25 /**< LED brightness (0-255) */




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
RTC_DATA_ATTR static rtc_data_t rtc_data;                              /**< Persists across deep sleep */
static BLEAdvertising* pAdvertising = nullptr;                         /**< BLE advertising handle */
static Adafruit_NeoPixel led(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800); /**< Status LED */




/* ============= Function Prototypes ============= */
/* Core State Functions */
static bool initializeHardware(void);
static void enterFactoryMode(void);
static void enterNormalMode(void);
static void handleError(const ErrorCode& error);


/* Hardware Control */
static bool setupLed(void);
// static void setLedColor(uint8_t r, uint8_t g, uint8_t b);
static void setLedColor(const uint8_t& r, const uint8_t& g, const uint8_t& b);
static void disableUnusedPins(void);

/* Security Functions */
static uint32_t generateSeed(void);
static uint32_t generateRollingCode(void);

/* BLE Functions */
static bool setupBLE(void);
static void broadcastBeacon(const uint32_t& code);


/* Utility Functions */
static void printDebugInfo(uint32_t code);
static String getMacAddress(void);



/**
 * @brief Hardware initialization
 * @return bool true if all initializations successful, false otherwise
 */
static bool initializeHardware(void) {
  bool success = true;

  Serial.println(F("[HARDWARE] Initializing..."));
  Serial.printf("[HARDWARE] Current State: %d\n", static_cast<int>(rtc_data.state));

  // Configure status LED
  if (!setupLed()) {
    Serial.println(F("[ERROR] LED Setup Failed"));
    success = false;
    rtc_data.lastError = ErrorCode::LED_INIT_FAILED;
  }

  // Configure BOOT button with internal pullup
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // Disable unused pins
  disableUnusedPins();

  // ** IMPORTANT: Always try to setup BLE, regardless of state
  Serial.println(F("[BLE] Attempting to setup"));
  if (!setupBLE()) {
    Serial.println(F("[ERROR] BLE Setup Failed"));
    success = false;
    rtc_data.lastError = ErrorCode::BLE_INIT_FAILED;
  }

  Serial.printf("[HARDWARE] Initialization %s\n", success ? "SUCCESS" : "FAILED");
  return success;
}




/**
 * @brief Arduino setup function
 */
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[INIT] Starting Emergency Beacon..."));

  // Validate RTC memory initialization
  if (rtc_data.magic != RTC_DATA_MAGIC) {
    // First-time or corrupted RTC memory
    memset(&rtc_data, 0, sizeof(rtc_data));
    rtc_data.magic = RTC_DATA_MAGIC;
    rtc_data.state = DeviceState::UNINITIALIZED;
    rtc_data.is_initialized = false;
    rtc_data.lastError = ErrorCode::NONE;
  }

  // Initialize hardware
  if (!initializeHardware()) {
    handleError(rtc_data.lastError);
    return;
  }

  // Determine operation mode
  if (!rtc_data.is_initialized || (esp_reset_reason() == ESP_RST_POWERON && digitalRead(BOOT_PIN) == LOW)) {
    rtc_data.state = DeviceState::FACTORY_MODE;
    Serial.println(F("[WARNING] Factory reset required"));
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
  Serial.println(F("[BLE] Initializing..."));

  // Deinitialize BLE first to ensure clean state
  BLEDevice::deinit(true);

  // Try to initialize
  try {
    BLEDevice::init(PRODUCT_NAME);

    // Set minimum TX power for 2m range
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);  // -12dBm

    pAdvertising = BLEDevice::getAdvertising();

    if (pAdvertising == nullptr) {
      Serial.println(F("[ERROR] BLE Advertising Object is NULL"));
      return false;
    }

    pAdvertising->setScanResponse(false);
    pAdvertising->setMinInterval(0x20);
    pAdvertising->setMaxInterval(0x40);

    Serial.println(F("[BLE] Setup Complete"));
    return true;
  } catch (std::exception& e) {
    Serial.printf("[ERROR] BLE Exception: %s\n", e.what());
    return false;
  }
}



/**
 * @brief Get device MAC address
 * @return String MAC address in XX:XX:XX:XX:XX:XX format
 */
static String getMacAddress(void) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_IEEE802154);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(mac_str);
}



/**
 * @brief Generate unique device seed
 * @return uint32_t Unique seed value
 */
static uint32_t generateSeed(void) {
  uint8_t macAddr[6];
  esp_read_mac(macAddr, ESP_MAC_IEEE802154);

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
  Serial.println(F("\n[FACTORY] Entering Factory Reset Mode"));

  // LED Status: Factory Mode - Red
  setLedColor(255, 0, 0);

  // Generate and store new seed
  rtc_data.seed = generateSeed();
  rtc_data.counter = 0;

  // Print device information
  Serial.printf("[FACTORY] Device MAC: %s\n", getMacAddress().c_str());
  Serial.printf("[FACTORY] Generated Seed: 0x%08lX\n", rtc_data.seed);
  Serial.printf("[FACTORY] Will await 20 sec to jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation.\n");

  // Wait for button press or timeout
  uint32_t start_time = millis();
  while (millis() - start_time < FACTORY_WAIT_MS) {
    if (digitalRead(BOOT_PIN) == LOW) {
      Serial.println(F("[FACTORY] Button press detected"));
      delay(100);  // Debounce
      break;
    }
    delay(100);
  }

  // Transition to normal operation (steps)
  rtc_data.is_initialized = true;
  rtc_data.state = DeviceState::NORMAL_MODE;
  Serial.println(F("[FACTORY] Transitioning to Normal Mode"));
  // Allow serial to flush
  delay(100);
  Serial.flush();
  delay(100);

  enterNormalMode();
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
  Serial.println(F("\n[NORMAL] Entering Normal Operation Mode"));

  // LED Status: Active/Normal - Green
  setLedColor(0, 255, 0);

  // Core operations:
  // 1. Generate
  // 2. Broadcast Rolling code)
  uint32_t rolling_code = generateRollingCode();
  printDebugInfo(rolling_code);
  broadcastBeacon(rolling_code);

  rtc_data.counter++;

  // ==== Prepare for sleep ==== //
  Serial.println(F("[NORMAL] Entering deep sleep"));

  // 1.  Allow serial to flush
  delay(100);
  Serial.flush();
  delay(100);

  // 2.  Configure wakeup on GPIO
  const uint64_t ext_wakeup_pin_1_mask = 1ULL << BOOT_PIN;
  esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_LOW);

  // LED Status: Active/Normal - off
  setLedColor(0, 0, 0);

  esp_deep_sleep_start();
}




// /**
//  * @brief Broadcasts rolling code over BLE advertisement with custom formatting
//  * @details Packet structure [9 bytes total]:
//  *   - Header: MANUFACTURER_ID [2B]
//  *   - Type: Rolling code identifier [1B]
//  *   - Length: Payload length [1B]
//  *   - Payload: Rolling code [4B]
//  *   - CRC: Checksum [1B]
//  * 
//  * @param code 32-bit rolling code to broadcast
//  */
// static void broadcastBeacon(const uint32_t& code) {
//   if (!pAdvertising) {
//     handleError(ErrorCode::BLE_INIT_FAILED);
//     return;
//   }

//   uint8_t packet[9];
//   packet[0] = MANUFACTURER_ID & 0xFF;
//   packet[1] = (MANUFACTURER_ID >> 8) & 0xFF;
//   packet[2] = 0x01;
//   packet[3] = 0x04;

//   packet[4] = (code >> 24) & 0xFF;
//   packet[5] = (code >> 16) & 0xFF;
//   packet[6] = (code >> 8) & 0xFF;
//   packet[7] = code & 0xFF;

//   uint8_t crc = 0;
//   for (int i = 0; i < 8; i++) {
//     crc ^= packet[i];
//   }
//   packet[8] = crc;

//   BLEAdvertisementData advData;
//   advData.setName(PRODUCT_NAME);
//   String data;
//   for (int i = 0; i < sizeof(packet); i++) {
//     data += (char)packet[i];
//   }
//   advData.setManufacturerData(data);
//   pAdvertising->setAdvertisementData(advData);

//   pAdvertising->start();
//   delay(BEACON_TIME_MS);
//   pAdvertising->stop();
// }



/**
* @brief Broadcasts rolling code via BLE advertising
* @param code 32-bit rolling code to broadcast
* 
* Function flow:
* 1. Validates BLE initialization
* 2. Splits 32-bit code into 4 bytes
* 3. Creates BLE advertisement payload
* 4. Broadcasts for BEACON_TIME_MS duration
*/
static void broadcastBeacon(const uint32_t& code) {
  // Check BLE initialization
  if (!pAdvertising) {
    Serial.println(F("[ERROR] BLE not initialized"));
    return;
  }

  // Split 32-bit code into byte array
  uint8_t payload[4];
  payload[0] = (code >> 24) & 0xFF;
  payload[1] = (code >> 16) & 0xFF;
  payload[2] = (code >> 8) & 0xFF;
  payload[3] = code & 0xFF;

  // Create advertisement data
  BLEAdvertisementData advData;
  advData.setName(PRODUCT_NAME);
  String data;
  for (int i = 0; i < 4; i++) {
    data += (char)payload[i];  // Convert bytes to chars
  }
  advData.setManufacturerData(data);  // Set payload
  pAdvertising->setAdvertisementData(advData);

  // Start advertising for specified duration
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

  // Detailed error logging
  switch (error) {
    case ErrorCode::BLE_INIT_FAILED:
      Serial.println(F("[CRITICAL] BLE Initialization Failed"));
      Serial.print(F("[DEBUG] Advertising Pointer: "));
      Serial.println(reinterpret_cast<uintptr_t>(pAdvertising));
      break;
    case ErrorCode::LED_INIT_FAILED:
      Serial.println(F("[CRITICAL] LED Initialization Failed"));
      break;
    case ErrorCode::INVALID_STATE:
      Serial.println(F("[CRITICAL] Invalid Device State"));
      break;
    default:
      Serial.println(F("[CRITICAL] Unknown Error"));
  }

  // Error indication - Red blink
  for (int i = 0; i < 5; i++) {
    setLedColor(255, 0, 0);
    delay(100);
    setLedColor(0, 0, 0);
    delay(100);
  }

  // ** Optional: Soft reset
  ESP.restart();
}



/**
 * @brief Print debug information to serial
 * @param code Current rolling code value
 */
static void printDebugInfo(uint32_t code) {
  Serial.println(F("\n=== Debug Information ==="));
  Serial.printf("MAC Address: %s\n", getMacAddress().c_str());
  Serial.printf("Product Key: 0x%08lX\n", PRODUCT_KEY);
  Serial.printf("Batch ID: 0x%04X\n", BATCH_ID);
  Serial.printf("Current Seed: 0x%08lX\n", rtc_data.seed);
  Serial.printf("Counter: %lu\n", rtc_data.counter);
  Serial.printf("Rolling Code: 0x%08lX\n", code);
  Serial.printf("Algorithm: Mixed-bit with time seed\n");
  Serial.println(F("=======================\n"));
}
