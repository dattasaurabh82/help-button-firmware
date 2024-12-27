/**
* @file    secure_ble_beacon.ino
* @brief   Secure BLE Emergency Beacon Implementation for ESP32-H2
* @details Implements secure rolling code beacon with factory reset and sleep modes
* 
* @author  Saurabh Datta | Datta+Baum Studio
* @date    2024-12
* @version 0.0.1
* @license GPL 3.0
*
* @note    Built for ESP32-H2 platform
* @warning Requires specific hardware configuration
*
* @dependencies
*   - BLE Library
*   - Adafruit_NeoPixel
*/

/**
* The BLE config defines can be removed since they're in sdkconfig.h
* So, if furthr optimization needed, do it there. 
 */
// #define CONFIG_BT_BLE_50_FEATURES_SUPPORTED 0
// #define CONFIG_BT_BLE_42_FEATURES_SUPPORTED 1
// #define CONFIG_BT_CTRL_MODE_BR_EDR_ONLY 0
// #define CONFIG_BT_CTRL_MODE_EFF 1

#include "config.h"  // Product configuration macros
// #include "debug.h"            // Debug macros
// #include "hardware_config.h"  // Hardware configuration

#include <BLEDevice.h>
#include <BLEAdvertising.h>

#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_bt.h>
#include "esp_mac.h"
#include <driver/rtc_io.h>

#include <Adafruit_NeoPixel.h>.  // For the status LED

/* ============= Configuration Constants ============= */
#define PRODUCT_NAME "HELP by JENNYFER" /**< Product-specific name */

/* ========== NOTE ========== */
// * Moved to Macros (config.h)
// #define PRODUCT_KEY 0xXXX   /**< Product-specific key */
// #define BATCH_ID 0xXXX      /**< Production batch identifier */
// * Create a file called config.h by following the config_template.h and for the values themselves, contact the developer :)
/* ========================== */

#define BOOT_PIN 9            /**< GPIO pin for BOOT button */
#define BEACON_TIME_MS 10000  /**< Broadcast duration in ms */
#define FACTORY_WAIT_MS 60000 /**< Factory reset timeout in ms */

/* ============= Type Definitions ============= */
typedef struct {
  uint32_t seed;       /**< Device-specific seed */
  uint32_t counter;    /**< Rolling code counter */
  bool is_initialized; /**< Initialization flag */
} rtc_data_t;

/* ============= Global Variables ============= */
RTC_DATA_ATTR static rtc_data_t rtc_data; /**< Persists across deep sleep */
BLEAdvertising* pAdvertising = nullptr;   /**< BLE advertising handle */


/* ====== Status LED setup (DEBUG Only) ======= */
const uint8_t ledPin = 8;
Adafruit_NeoPixel led(1, ledPin, NEO_GRB + NEO_KHZ800);

/**
 * @brief LED setup func
 * @details Initializes GPIO8 as output and pulls it low
 */
inline void setupLed() {
  led.begin();
  led.setBrightness(50);  // 0-255
  led.clear();
  led.show();
}

/**
 * @brief LED setup func
 * @details setsup RED LED 
 */
inline void setLedRed() {
  led.setPixelColor(0, led.Color(255, 0, 0));
  led.show();
}
/**
 * @brief LED setup func
 * @details setsup GREEN LED 
 */
inline void setLedGreen() {
  led.setPixelColor(0, led.Color(0, 255, 0));
  led.show();
}

/**
 * @brief LED setup func
 * @details Turns off all LEDs
 */
inline void setLedOff() {
  led.clear();
  led.show();
}



/* Function Prototypes */
void enterFactoryMode(void);
void enterNormalMode(void);
uint32_t generateSeed(void);
uint32_t generateRollingCode(void);
void setupBLE(void);
void broadcastBeacon(uint32_t code);
void printDebugInfo(uint32_t code);
void disableUnusedPins(void);
String getMacAddress(void);




/**
 * @brief Arduino setup function
 * @details Initializes device and determines operation mode
 */
void setup() {

  Serial.begin(115200);
  Serial.println("\n[INIT] Starting Emergency Beacon...");

  // WIP
  disableUnusedPins();

  // Configure status LED
  setupLed();

  // Configure BOOT button with internal pullup
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // Check operation mode:
  // Only enter Factory Mode if not initialized OR reset button held during power-up
  if (!rtc_data.is_initialized || (esp_reset_reason() == ESP_RST_POWERON && digitalRead(BOOT_PIN) == LOW)) {
    Serial.println(F("[WARNING] Factory reset required"));
    enterFactoryMode();
  } else {
    enterNormalMode();
  }
}


/**
 * @brief Main loop function (unused - device sleeps between operations)
 */
void loop() {
  delay(1000);
}

/**
 * @brief Handles factory reset mode operations
 * @details Generates new seed, displays debug info, waits for confirmation
 */
void enterFactoryMode(void) {
  Serial.println("\n[FACTORY] Entering Factory Reset Mode");

  setLedRed();  // Red ON, Green OFF

  // Generate and store new seed
  rtc_data.seed = generateSeed();
  rtc_data.counter = 0;

  // Print device information
  Serial.printf("[FACTORY] Device MAC: %s\n", getMacAddress().c_str());
  Serial.printf("[FACTORY] Generated Seed: 0x%08lX\n", rtc_data.seed);
  Serial.printf("[FACTORY] Take a note.\n[FACTORY] Will await 60 sec t jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation.\n");

  // Wait for button press or timeout
  uint32_t start_time = millis();
  while (millis() - start_time < FACTORY_WAIT_MS) {
    if (digitalRead(BOOT_PIN) == LOW) {
      Serial.println("[FACTORY] Button press detected");
      delay(100);  // Debounce
      break;
    }
    delay(100);
  }

  // Mark as initialized and transition
  rtc_data.is_initialized = true;
  Serial.println("[FACTORY] Transitioning to Normal Mode");
  delay(100);  // Allow serial to flush
  enterNormalMode();
}

/**
 * @brief Handles normal operation mode
 * @details Generates rolling code, broadcasts, then enters deep sleep
 */
void enterNormalMode(void) {
  Serial.println("\n[NORMAL] Entering Normal Operation Mode");

  setLedGreen();  // Green ON, Red OFF

  // Generate new rolling code
  uint32_t rolling_code = generateRollingCode();

  // Setup BLE and broadcast
  setupBLE();
  printDebugInfo(rolling_code);
  broadcastBeacon(rolling_code);

  // Increment counter and prepare for sleep
  rtc_data.counter++;
  Serial.println("[NORMAL] Entering deep sleep");
  delay(100);  // Allow serial to flush

  // Configure wakeup on GPIO
  const uint64_t ext_wakeup_pin_1_mask = 1ULL << BOOT_PIN;
  esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_LOW);

  setLedOff();  // set both LEDs OFF

  esp_deep_sleep_start();
}

/**
 * @brief Generates unique device seed
 * @return uint32_t Unique seed value
 */
uint32_t generateSeed(void) {
  uint8_t macAddr[6];
  esp_read_mac(macAddr, ESP_MAC_IEEE802154);

  // XOR mixing of MAC, BATCH_ID, and PRODUCT_KEY
  uint32_t seed = PRODUCT_KEY;
  seed ^= ((uint32_t)BATCH_ID << 16);
  seed ^= ((macAddr[0] << 24) | (macAddr[1] << 16) | (macAddr[2] << 8) | macAddr[3]);
  return seed;
}


/**
 * @brief Generates rolling code based on seed and counter
 * @return uint32_t New rolling code value
 */
uint32_t generateRollingCode(void) {
  uint32_t timestamp = esp_timer_get_time() & 0xFFFFFF;
  uint32_t base = rtc_data.seed;

  // Multi-stage mixing
  uint32_t mixed = (base ^ timestamp) * 0x7FFF;
  mixed = mixed ^ (mixed >> 13);
  mixed = mixed * 0x5C4D;
  mixed = mixed ^ (mixed >> 17);
  mixed = mixed * rtc_data.seed;
  mixed = mixed ^ (mixed >> 16);

  return mixed;
}

/**
 * @brief Initializes BLE advertising
 */
void setupBLE(void) {
  BLEDevice::init(PRODUCT_NAME);  // Changed from "Emergency_Beacon"
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);
}



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
void broadcastBeacon(uint32_t code) {
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
 * @brief Prints debug information to serial
 * @param code Current rolling code
 */
void printDebugInfo(uint32_t code) {
  Serial.println("\n=== Debug Information ===");
  Serial.printf("MAC Address: %s\n", getMacAddress().c_str());
  Serial.printf("Product Key: 0x%08lX\n", PRODUCT_KEY);
  Serial.printf("Batch ID: 0x%04X\n", BATCH_ID);
  Serial.printf("Current Seed: 0x%08lX\n", rtc_data.seed);
  Serial.printf("Counter: %lu\n", rtc_data.counter);
  Serial.printf("Rolling Code: 0x%08lX\n", code);
  Serial.printf("Algorithm: Mixed-bit with time seed\n");
  Serial.println("=======================\n");
}

/**
 * @brief Gets device MAC address as string
 * @return String MAC address
 */
String getMacAddress(void) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_IEEE802154);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(mac_str);
}


/**
 * @brief disables floating unused pins
 */
void disableUnusedPins(void) {
  const gpio_num_t unusedPins[] = {
    GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5,
    GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_11, GPIO_NUM_12
  };

  for (gpio_num_t pin : unusedPins) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
  }
}