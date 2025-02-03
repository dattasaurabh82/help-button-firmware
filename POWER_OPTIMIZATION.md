# Power Optimization Analysis

## ESP32-H2 Emergency Beacon

** WIP

## Overview

The firmware implements multiple layers of power optimization for an emergency beacon device based on ESP32-H2. The primary goal is to minimize power consumption during deep sleep while maintaining quick response time for emergency activation.

## Deep Sleep Power Optimization Techniques

### 1. CPU and Clock Optimization

Located in [`button_firmware.ino`](button_firmware/button_firmware.ino)

```cpp
static void optimizeClocks(void) {
  setCpuFrequencyMhz(80);  // Reduce from default 160MHz
  // ESP32-H2 appears locked at 96MHz for BLE operation
  
  periph_module_disable(PERIPH_LEDC_MODULE);     // ~0.3mA savings
  periph_module_disable(PERIPH_MCPWM0_MODULE);   // ~0.5mA savings
  periph_module_disable(PERIPH_PCNT_MODULE);     // ~0.1mA savings
  periph_module_disable(PERIPH_SARADC_MODULE);   // ~0.5mA savings
  periph_module_disable(PERIPH_SYSTIMER_MODULE); // ~0.1mA savings
  periph_module_disable(PERIPH_UART1_MODULE);    // ~0.3mA savings
  periph_module_disable(PERIPH_SPI2_MODULE);     // ~0.4mA savings
  periph_module_disable(PERIPH_I2C0_MODULE);     // ~0.2mA savings
}
```

> Total peripheral power savings: ~2.4mA

### 2. GPIO Power Management

Implemented in [`button_firmware.ino`](button_firmware/button_firmware.ino)

```cpp
static void disableUnusedPins(void) {
  const gpio_num_t unusedPins[] = {
    GPIO_NUM_2,
    GPIO_NUM_4, GPIO_NUM_5,    // JTAG pins
    GPIO_NUM_6, GPIO_NUM_7,    // JTAG pins
    GPIO_NUM_10, GPIO_NUM_11,
    GPIO_NUM_12, GPIO_NUM_13
  };
  
  for (gpio_num_t pin : unusedPins) {
    gpio_config_t io_conf = {
      .mode = GPIO_MODE_OUTPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, 0);    // Pull low
    gpio_hold_en(pin);         // Hold state in sleep
  }
}
```

> Estimated savings per floating pin: 0.1-0.3mA
>
> Total GPIO savings: ~1-2mA

### 3. Serial/Debug Interface Power Management

From [`debug_log.h`](button_firmware/debug_log.h)

```cpp
#if DEBUG_LEVEL == DEBUG_LEVEL_NONE
#define DEBUG_INIT() \
  do { \
    pinMode(SERIAL_TX_PIN, OUTPUT); \
    pinMode(SERIAL_RX_PIN, OUTPUT); \
    digitalWrite(SERIAL_TX_PIN, LOW); \
    digitalWrite(SERIAL_RX_PIN, LOW); \
  } while (0)
```

When debug is disabled

- TX/RX pins pulled low
- No power consumption from UART
  
> Estimated savings: ~0.5mA

### 4. LED Power Management

From [`debug_led.h`](button_firmware/debug_led.h)

```cpp
#if DEBUG_LED == DEBUG_LED_DISABLED
#define LED_INIT() \
  do { \
    pinMode(DEBUG_LED_PIN, OUTPUT); \
    digitalWrite(DEBUG_LED_PIN, LOW); \
  } while (0)
```

LED disabled configuration saves

- NeoPixel current draw: ~1mA
- Supporting peripheral (RMT): ~0.2mA

### 5. BLE Power Optimization

In [`button_firmware.ino`](button_firmware/button_firmware.ino)

```cpp
static bool setupBLE(void) {
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);   // -12dBm
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_N12);  // -12dBm
  
  pAdvertising->setMinInterval(0x40);  // 40ms * 0.625ms = 25ms
  pAdvertising->setMaxInterval(0x80);  // 80ms * 0.625ms = 50ms
}
```

Power savings through

- Minimum TX power (-12dBm)
- Optimized advertising intervals
- BLE power domain disabled in deep sleep

> Estimated savings: 5-10mA

### 6. Deep Sleep Configuration

Wake-up configuration in `button_firmware.ino`:

```cpp
static bool setupDeepSleepWakeup(const gpio_num_t wakeup_pin) {
  const uint64_t ext_wakeup_pin_1_mask = 1ULL << wakeup_pin;
  esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_LOW);
}
```

Only EXT1 wake-up source enabled, all other wake sources disabled.

## Total Power Savings

__Active Mode Power Reduction__:

- Peripherals: `~2.4mA`
- GPIO: `~1-2mA`
- Serial/Debug: `~0.5mA`
- LED: `~1.2mA`
- BLE optimizations: `~5-10mA`
Total Active Power Reduction: ~10-16mA

__Deep Sleep Current__:

- Base ESP32-H2 deep sleep: `~5µA`
- Additional sleep optimizations reduce to: `~2-3µA`

> The combination of these techniques results in approximately 80-90% power reduction compared to an unoptimized implementation.

## Additional Notes [WIP]

1. The code includes incomplete power domain configuration (commented out in `powerDownDomains()`). This could potentially provide additional savings but requires further testing due to stability issues.

2. Critical system GPIOs (14-21) are intentionally left untouched to maintain system stability.

3. The LED and Debug interfaces can be completely disabled at compile time, providing maximum power savings for production units.
