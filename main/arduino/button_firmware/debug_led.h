/**
 * @file    debug_led.h
 * @brief   Status LED control for BLE Emergency Beacon
 * @details Handles NeoPixel LED initialization and control
*/

#ifndef DEBUG_LED_H
#define DEBUG_LED_H

/* ============= LED Configuration ============= */
#define DEBUG_LED_DISABLED 0  // No LED functionality
#define DEBUG_LED_ENABLED 1   // Enable LED functionality

#ifndef DEBUG_LED
#define DEBUG_LED DEBUG_LED_DISABLED  // Default disabled
#endif

#define DEBUG_LED_PIN 8         /**< GPIO pin for NeoPixel LED */


#if DEBUG_LED == DEBUG_LED_ENABLED
#include <Adafruit_NeoPixel.h>

#define DEBUG_LED_BRIGHTNESS 25 /**< LED brightness (0-255) */

static Adafruit_NeoPixel statusLed(1, DEBUG_LED_PIN, NEO_GRB + NEO_KHZ800);

/**
  * @brief Initialize LED hardware
  * @return bool True if successful
*/
#define LED_INIT() \
  do { \
    statusLed.begin(); \
    statusLed.setBrightness(DEBUG_LED_BRIGHTNESS); \
    statusLed.clear(); \
    statusLed.show(); \
  } while (0)

/**
 * @brief Set LED color
*/
static void setLedColor(const uint8_t& r, const uint8_t& g, const uint8_t& b) {
  statusLed.setBrightness(DEBUG_LED_BRIGHTNESS);
  statusLed.setPixelColor(0, statusLed.Color(r, g, b));
  statusLed.show();
}

/**
 * @brief Common color macros
*/
#define LED_OFF() setLedColor(0, 0, 0)
#define LED_RED() setLedColor(255, 0, 0)
#define LED_YELLOW() setLedColor(255, 125, 0)
#define LED_GREEN() setLedColor(0, 255, 0)

#else  // DEBUG_LED == DEBUG_LED_DISABLED
/** 
 * @brief Functions when LED disabled -> Make the Debug LED pin LOW to prevent from floating 
 * @return  True; always
*/
#define LED_INIT() \
  do { \
    pinMode(DEBUG_LED_PIN, OUTPUT); \
    digitalWrite(DEBUG_LED_PIN, LOW); \
  } while (0)

/**
 * @brief Empty Set LED color
*/
static void setLedColor(const uint8_t&, const uint8_t&, const uint8_t&) {}

/**
 * @brief Empty Common color macros
*/
#define LED_OFF()
#define LED_RED()
#define LED_YELLOW()
#define LED_GREEN()

#endif  // DEBUG_LED == DEBUG_LED_DISABLED

#endif  // DEBUG_LED_H
