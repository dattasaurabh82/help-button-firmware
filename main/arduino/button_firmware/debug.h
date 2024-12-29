/**
* @file    debug.h
* @brief   Debug system for BLE Emergency Beacon
* @details Handles debug output and serial pin management
*/

#ifndef DEBUG_H
#define DEBUG_H

/* ============= Debug Configuration ============= */
#define DEBUG_LEVEL_NONE 0     // No debug output
#define DEBUG_LEVEL_VERBOSE 1  // All messages (info + errors + anything else)


#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL DEBUG_LEVEL_NONE
#endif

/* ============= Serial Pin Definitions ============= */
#define SERIAL_TX_PIN 1  // UART0 TX GPIO
#define SERIAL_RX_PIN 3  // UART0 RX GPIO


#if DEBUG_LEVEL == DEBUG_LEVEL_NONE
// Pull the serial pins to low
#define DEBUG_INIT() \
  do { \
    pinMode(SERIAL_TX_PIN, OUTPUT); \
    pinMode(SERIAL_RX_PIN, OUTPUT); \
    digitalWrite(SERIAL_TX_PIN, LOW); \
    digitalWrite(SERIAL_RX_PIN, LOW); \
  } while (0)
#endif



#if DEBUG_LEVEL == DEBUG_LEVEL_VERBOSE
/* ============= Debug Messages in PROGMEM ============= */
// Error Messages
static const char PROGMEM DBG_RTC_INIT[] = "[RTC] Memory validation failed - initializing";
static const char PROGMEM DBG_ERR_LED[] = "[ERROR] LED Setup Failed";
static const char PROGMEM DBG_ERR_BLE[] = "[ERROR] BLE Setup Failed";
static const char PROGMEM DBG_ERR_BLE_NULL[] = "[ERROR] BLE Advertising Object is NULL";
static const char PROGMEM DBG_ERR_BLE_EXCEPT[] = "[ERROR] BLE Exception: %s\n";
static const char PROGMEM DBG_ERR_BLE_UNINIT[] = "[ERROR] BLE not initialized";
static const char PROGMEM DBG_CRIT_BLE[] = "[CRITICAL] BLE Initialization Failed";
static const char PROGMEM DBG_CRIT_LED[] = "[CRITICAL] LED Initialization Failed";
static const char PROGMEM DBG_CRIT_STATE[] = "[CRITICAL] Invalid Device State";
static const char PROGMEM DBG_CRIT_UNKNOWN[] = "[CRITICAL] Unknown Error";

// Info Messages
static const char PROGMEM DBG_INIT[] = "\n[INIT] Starting Emergency Beacon...";
static const char PROGMEM DBG_HW_INIT[] = "\n[HARDWARE] Initializing...";
static const char PROGMEM DBG_HW_STATE[] = "\n[HARDWARE] Current State: %d";
static const char PROGMEM DBG_HW_RESULT[] = "\n[HARDWARE] Initialization %s";
static const char PROGMEM DBG_BLE_INIT[] = "\n[BLE] Initializing...";
static const char PROGMEM DBG_BLE_ATTEMPT[] = "\n[BLE] Attempting setup...";
static const char PROGMEM DBG_BLE_SETUP[] = "\n[BLE] Setup Complete";
static const char PROGMEM DBG_BLE_BROADCAST_WARN[] = "\n[BLE] Broadcasting beacon for: %d secs ...";
static const char PROGMEM DBG_FACTORY_WARN[] = "\n[WARNING] Factory reset required";
static const char PROGMEM DBG_FACTORY_ENTER[] = "\n[FACTORY] Entering Factory Reset Mode";
static const char PROGMEM DBG_FACTORY_MAC[] = "\n[FACTORY] Device MAC: %s";
static const char PROGMEM DBG_FACTORY_SEED[] = "\n[FACTORY] Generated Seed: 0x%08lX";
static const char PROGMEM DBG_FACTORY_WAIT[] = "\n[FACTORY] Will await 20 sec to jump to normal ops.\n[FACTORY] Or, press BOOT to jump to normal operation.";
static const char PROGMEM DBG_FACTORY_BTN[] = "\n[FACTORY] Button press detected";
static const char PROGMEM DBG_FACTORY_TRANS[] = "\n[FACTORY] Transitioning to Normal Mode";
static const char PROGMEM DBG_NORMAL_ENTER[] = "\n\n[NORMAL] Entering Normal Operation Mode";
static const char PROGMEM DBG_NORMAL_SLEEP[] = "\n[NORMAL] Entering deep sleep";

// Debug Info Messages
static const char PROGMEM DBG_DEBUG_START[] = "\n=== Debug Information ===";
static const char PROGMEM DBG_DEBUG_MAC[] = "\nMAC Address: %s";
static const char PROGMEM DBG_DEBUG_KEY[] = "\nProduct Key: 0x%08lX";
static const char PROGMEM DBG_DEBUG_BATCH[] = "\nBatch ID: 0x%04X";
static const char PROGMEM DBG_DEBUG_SEED[] = "\nCurrent Seed: 0x%08lX";
static const char PROGMEM DBG_DEBUG_COUNTER[] = "\nCounter: %lu";
static const char PROGMEM DBG_DEBUG_ROLLING_CODE[] = "\nRolling Code: 0x%08lX";
static const char PROGMEM DBG_DEBUG_ALGO[] = "\nAlgorithm: Mixed-bit with time seed";
static const char PROGMEM DBG_DEBUG_END[] = "\n==========================";

// Initialize Serial and configure pins
#define DEBUG_INIT() \
  do { \
    Serial.begin(115200); \
    delay(10); \
  } while (0)

// Cleanup Serial before sleep
#define DEBUG_DEINIT() \
  do { \
    Serial.end(); \
    pinMode(SERIAL_TX_PIN, OUTPUT); \
    pinMode(SERIAL_RX_PIN, OUTPUT); \
    digitalWrite(SERIAL_TX_PIN, LOW); \
    digitalWrite(SERIAL_RX_PIN, LOW); \
  } while (0)

// Flush Serial
#define DEBUG_FLUSH() \
  do { \
    delay(100); \
    Serial.flush(); \
    delay(100); \
  } while (0)

// Verbose level messages
#if DEBUG_LEVEL == DEBUG_LEVEL_VERBOSE
#define DEBUG_VERBOSE(msg) Serial.print(F(msg))
#define DEBUG_VERBOSE_F(fmt, ...) Serial.printf(F(fmt), __VA_ARGS__)
#else
#define DEBUG_VERBOSE(msg)
#define DEBUG_VERBOSE_F(fmt, ...)
#endif

#else
// Debug disabled - all macros are empty
#define DEBUG_INIT()
#define DEBUG_DEINIT()
#define DEBUG_FLUSH()
#define DEBUG_VERBOSE(msg)
#define DEBUG_VERBOSE_F(fmt, ...)
#endif

#endif  // DEBUG_H