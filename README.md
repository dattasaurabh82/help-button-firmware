# Firmware

| Type | Status |
|------|---------|
| Build status | [![Firmware Build](https://github.com/dattasaurabh82/help-button-firmware/actions/workflows/build_main_firmware.yml/badge.svg)](https://github.com/dattasaurabh82/help-button-firmware/actions/workflows/build_main_firmware.yml) |

---

## Hardware Preparation

### Dev Module

TBD

### Real hardware Module

TBD

---

## Firmware installation instructions

### 1. From the web (Just want to flash the firmware - Recommended and most straight forward)

TBD

### 2. arduino-cli (Just want to flash the firmware - But from your local machine)

TBD

### 3. Arduino IDE 2.3.4 (Easy for Development and want to look under the hood)

## Prerequisites

1. Install the latest Arduino IDE (version 2.3.4 or above) for your platform.
2. Open the Arduino IDE and navigate to the Board Manager.
3. Search for "esp32" and install the "esp32 by Espressif" (latest).

## Compile & upload

1. Clone this repository to your local machine.
2. Open the [button_firmware.ino](main/arduino/button_firmware/button_firmware.ino) file located in the [main/arduino/button_firmware](main) directory.
3. In the Arduino IDE, select the following board parameters:

   ```txt
   Board: ESP32-H2-Dev Module
   Upload Speed: 921600
   CDC On Boot: Disabled
   Flash Frequency: 64MHz
   Flash Mode: QIO
   Flash Size: 4MB (32Mb)
   Partition Scheme: Default 4MB (1.2MB APP/1.5MB SPIFFS)
   Erase Flash: Disabled
   JTAG Adapter: Disabled
   Zigbee Mode: Disabled
   ```

4. Select the appropriate USB Serial port for your device (in Win, make sure you ahve "xxx" drtiver installed and then select the right COM port; on mac and linux, you can ignore).
5. Click the Upload button to flash the firmware. It will compile and upload

### 4. Platform IO (If you are a pro and are keen in firmware development)

TBD

## Main and test source code files

TBD

## Auto-compilation pipeline

TBD

## Firmware flashing instructions

TBD

## License

[GPL-3.0](LICENSE)

## Attribution

```txt
Saurabh Datta
Dec 2024
Berlin, Germany
```
