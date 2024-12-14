/**
 * Emergency Beacon Device - Base Implementation
 * 
 * This code implements a secure BLE emergency beacon using ESP32-H2
 * with power-efficient state management and secure rolling codes.
 * 
 * Operation Modes:
 * - FACTORY_NEW: Initial state, ready for setup
 * - SETUP_PENDING: Broadcasting device info for registration
 * - SETUP_COMPLETE: Normal operation with rolling codes
 * - RECOVERY: Handling power loss and state recovery
 * 
 * Security Features:
 * - Unique device seed from MAC + BatchID
 * - Rolling code implementation
 * - Power loss protection
 * - State validation
 * 
 * Hardware Requirements:
 * - ESP32-H2 module
 * - BOOT button (GPIO9)
 * 
 * Note: For production, change MANUFACTURER_ID from 0xFFFF to your
 * assigned Bluetooth SIG company identifier

  ----------------------------------
  DEVICE STATES
  ----------------------------------
  A. FACTORY_NEW
    - Happens after 1st time uploading code
    - Fresh device, no configuration
    - Contains only programmed MAC and default keys
    - Ready for setup

  B. SETUP_PENDING
    - Device powered on first time
    - Broadcasting device info for registration
    - Waiting for app pairing
    - Limited time window (e.g., 5 minutes)

  C. SETUP_COMPLETE
    - Normal operation mode
    - Ready for button triggers
    - Deep sleep capable
    - Rolling code active

  D. RECOVERY
    - Handles power loss
    - Validates stored state
    - Can reenter setup if needed
  ----------------------------------

  ----------------------------------
  BUTTON INTERACTIONS
  ----------------------------------
  A. Normal Operation:
    - Single press: Wake & broadcast
    - Sleep after broadcast

  B. Setup Entry:
    - From OFF state: Hold BOOT while powering on
    - OR: 5 quick presses within 3 seconds

  C. Setup Exit:
    - Timeout after 5 minutes
    - OR: Successful registration
    - OR: Single long press
  ----------------------------------

  -----------------------------------------------
  STATE STORAGE & ROLLING CODE RECOVERY MECHANISM
  -----------------------------------------------
  A. RTC Memory (Survives deep sleep):
    - Current rolling code
    - Wake count
    - Operation mode

  B. Flash Memory (Survives power loss):
    - Device state
    - Registration status
    - Batch/Product keys
    - Last valid code
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include "esp_sleep.h"
#include <Preferences.h>
#include <esp_system.h>
#include "esp_bt.h"   // Add this for ESP_MAC_BT
#include "esp_mac.h"  // Add this for esp_read_mac

// Device Configuration
#define MANUFACTURER_ID 0xFFFF     // ** Development ID (Change for production)
#define PRODUCT_TYPE 0x01          // Product type identifier
const uint16_t BATCH_ID = 0x0001;  // Changed to const for proper reference

// Timing Configuration
#define SETUP_TIMEOUT 300000  // Setup mode timeout (5 minutes)
#define ADVERTISE_TIME 10000  // Normal advertisement time (10 seconds)
#define BUTTON_INTERVAL 250   // Debounce time (milliseconds)

// Security Parameters
#define PRODUCT_KEY 0x12345678  // ** Product line key (Change per product line)

// State Management
enum DeviceState {
  FACTORY_NEW = 0,
  SETUP_PENDING,
  SETUP_COMPLETE,
  RECOVERY
};

// Fix printf format warnings
#define PRINTF_UINT32 "lu"  // Use for uint32_t printing

// Rolling code secure & unique related vars
// - RTC Memory Variables
// - persist during sleep
RTC_DATA_ATTR static uint32_t rollCode = 0;
RTC_DATA_ATTR static uint32_t wakeCount = 0;
RTC_DATA_ATTR static DeviceState currentState = FACTORY_NEW;
RTC_DATA_ATTR static uint32_t deviceSeed = 0;

// Global Objects
Preferences preferences;
BLEAdvertising *pAdvertising = nullptr;

// Function Declarations
void initializeDevice();
void handleSetupMode();
void handleNormalOperation();
void handleRecovery();
bool checkSetupEntry();
void generateDeviceSeed();
uint32_t generateRollingCode();
void configureAdvertising(bool isSetup);

/**
 * Device Initialization and Core Functions
 */

// Flash storage keys
#define PREF_STATE "devState"
#define PREF_SEED "devSeed"
#define PREF_CODE "lastCode"
#define PREF_COUNT "wakeCount"

/**
 * Generate unique device seed from hardware identifiers
 */
void generateDeviceSeed() {
  uint8_t macAddr[6];
  esp_err_t result = esp_read_mac(macAddr, ESP_MAC_BT);
  if (result != ESP_OK) {
    Serial.println("Failed to read MAC address");
    return;
  }

  // Combine MAC, BATCH_ID, and PRODUCT_KEY for unique seed
  deviceSeed = PRODUCT_KEY ^ ((uint32_t)BATCH_ID << 16) ^ ((macAddr[0] << 24) | (macAddr[1] << 16) | (macAddr[2] << 8) | macAddr[3]);

  preferences.putUInt(PREF_SEED, deviceSeed);
  Serial.printf("Device Seed Generated: 0x%08" PRINTF_UINT32 "\n", deviceSeed);
}

/**
 * Check for setup mode entry conditions
 * Returns true if setup mode should be entered
 */
bool checkSetupEntry() {
  static uint32_t pressCount = 0;
  static uint32_t lastPress = 0;

  uint32_t now = millis();

  // Check for button press pattern
  if ((now - lastPress) < 3000) {
    pressCount++;
    if (pressCount >= 5) {
      return true;
    }
  } else {
    pressCount = 1;
  }
  lastPress = now;

  return false;
}

/**
 * Generate next rolling code
 */
uint32_t generateRollingCode() {
  uint32_t timestamp = esp_timer_get_time() & 0xFFFFFF;
  uint32_t base = (rollCode == 0) ? deviceSeed : rollCode;

  // Multi-stage mixing function
  uint32_t mixed = (base ^ timestamp) * 0x7FFF;
  mixed = mixed ^ (mixed >> 13);
  mixed = mixed * 0x5C4D;
  mixed = mixed ^ (mixed >> 17);
  mixed = mixed * deviceSeed;
  mixed = mixed ^ (mixed >> 16);

  return mixed;
}

/**
 * Configure BLE advertising based on mode
 */
void configureAdvertising(bool isSetup) {
  if (pAdvertising == nullptr) {
    pAdvertising = BLEDevice::getAdvertising();
  }

  BLEAdvertisementData advData;
  uint8_t payload[12];

  // Common header
  payload[0] = MANUFACTURER_ID & 0xFF;
  payload[1] = (MANUFACTURER_ID >> 8) & 0xFF;
  payload[2] = PRODUCT_TYPE;

  if (!isSetup) {
    uint32_t code = generateRollingCode();
    payload[3] = (code >> 24) & 0xFF;
    payload[4] = (code >> 16) & 0xFF;
    payload[5] = (code >> 8) & 0xFF;
    payload[6] = code & 0xFF;
    payload[7] = wakeCount & 0xFF;

    Serial.println("\nBroadcast Payload:");
    Serial.printf("Manufacturer ID: 0x%04X\n", MANUFACTURER_ID);
    Serial.printf("Product Type: 0x%02X\n", PRODUCT_TYPE);
    Serial.printf("Rolling Code: 0x%08X\n", code);
    Serial.printf("Wake Count: %d\n", wakeCount);
  }

  String manufacturerData;
  for (int i = 0; i < (isSetup ? 12 : 8); i++) {
    manufacturerData += (char)payload[i];
  }

  advData.setManufacturerData(manufacturerData);
  pAdvertising->setAdvertisementData(advData);
}

/**
 * Handle setup mode operation
 */
void handleSetupMode() {
  Serial.println("Entering setup mode...");

  BLEDevice::init("Emergency Setup");
  configureAdvertising(true);

  // Start advertising
  pAdvertising->start();

  uint32_t setupStart = millis();
  while ((millis() - setupStart) < SETUP_TIMEOUT) {
    // TODO: Add setup confirmation check
    delay(100);
  }

  // Exit setup mode
  pAdvertising->stop();
  currentState = SETUP_COMPLETE;
  preferences.putUInt(PREF_STATE, SETUP_COMPLETE);
}

/**
 * Handle normal operation
 */
void handleNormalOperation() {
  wakeCount++;

  BLEDevice::init("Emergency Beacon");
  configureAdvertising(false);

  pAdvertising->start();
  Serial.printf("Broadcasting - Wake Count: %" PRINTF_UINT32 "\n", wakeCount);

  delay(ADVERTISE_TIME);
  pAdvertising->stop();

  if (wakeCount % 10 == 0) {
    preferences.putUInt(PREF_COUNT, wakeCount);
    preferences.putUInt(PREF_CODE, rollCode);
  }
}


void setup() {
  Serial.begin(115200);
  delay(3000);  // Give serial time to stabilize

  Serial.println("\n=== Emergency Beacon ===");
  // Serial.println("\n\n=== Emergency Beacon Setup ===");
  // Serial.println("NOTE: Current implementation is simplified:");
  // Serial.println("- Skips proper setup state management");
  // Serial.println("- Moves directly from FACTORY_NEW to normal operation");
  // Serial.println("- Setup mode and verification to be implemented in next sprint");
  // Serial.println("=====================================\n");

  preferences.begin("beacon", false);

  if (esp_reset_reason() == ESP_RST_POWERON) {
    Serial.println("\n===Power-on reset detected===");
    currentState = static_cast<DeviceState>(preferences.getUInt(PREF_STATE, FACTORY_NEW));

    if (currentState == FACTORY_NEW) {
      Serial.println("=== Setup Needed ===");
      Serial.println("FACTORY NEW DEVICE - Generating seed...");
      generateDeviceSeed();
      Serial.println("\n=== IMPORTANT: RECORD THESE VALUES ===");
      uint8_t macAddr[6];
      esp_read_mac(macAddr, ESP_MAC_BT);
      Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    macAddr[0], macAddr[1], macAddr[2],
                    macAddr[3], macAddr[4], macAddr[5]);
      Serial.printf("Device Seed: 0x%08" PRINTF_UINT32 "\n", deviceSeed);
      Serial.printf("Batch ID: 0x%04X\n", BATCH_ID);
      Serial.println("=====================================\n");
      Serial.println("WARNING: Proceeding directly to normal operation");
      Serial.println("(Proper setup state management to be implemented)");
      Serial.println("\nWaiting 5 sec to note values...");
      delay(5000);  // Give time to note values
    }
  }

  // Configure wake-up source
  uint64_t mask = (1ULL << 9);  // GPIO 9 (BOOT button)
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  // Check for setup mode entry
  if (checkSetupEntry()) {
    handleSetupMode();
  } else {
    handleNormalOperation();
  }

  // Enter deep sleep
  Serial.println("Entering deep sleep");
  delay(100);  // Allow serial to complete
  esp_deep_sleep_start();
}

void loop() {
  // Never reached - device is in deep sleep
}