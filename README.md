# Firmware

| Type | Status |
|------|---------|
| Build status | [![Firmware Build](https://github.com/dattasaurabh82/help-button-firmware/actions/workflows/build_main_firmware.yml/badge.svg)](https://github.com/dattasaurabh82/help-button-firmware/actions/workflows/build_main_firmware.yml) |

---

## Hardware Preparation

<details>
<summary>"Why ESP-32", you may ask</summary>

We have chosen [ESP32-H2-MINI-1](https://www.espressif.com/sites/default/files/documentation/esp32-h2-mini-1_mini-1u_datasheet_en.pdf) for its natural advantages:

1. The ESP family is developer-friendly, with the ESP SDK being widely supported across various frameworks like Arduino and PIO, in addition to ESP-IDF itself. This makes long-term development maintenance much easier.
2. This specific module has the lowest deep sleep power consumption in the entire ESP family (as of December 2024).
3. It is widely available and cost-effective.
4. Firmware flashing is simpler compared to previous microcontrollers, requiring no development environment. For example, web flashing capabilities mean factories can easily flash devices during mass production without setting up specific development environments.
5. It has a smaller footprint.
6. It features built-in BLE and WiFi, plus support for future protocols like Thread, making it future-proof.
7. It has all [necessary certifications](https://www.espressif.com/en/support/documents/certificates?keys=&field_product_value%5B%5D=ESP32-H2&field_product_value%5B%5D=ESP32-H2-MINI-1) for shipping radio-based consumer electronics.

</details>

<details>
<summary>Development module/kit</summary>

To test development and firmware, you can purchase [ESP32-H2-DevKitM-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32h2/esp32-h2-devkitm-1/user_guide.html)

![alt text](<assets/dev module info.png>)

![alt text](assets/esp32-h2-devkitm-1-v1.2_pinlayout.png)

🛒 [Purchase link](https://amzn.eu/d/6zMkRbX)
</details>

<details>
<summary>Real hardware module</summary>

TBD
</details>

---

## Firmware installation instructions

<details>
<summary>1. From the web (Just want to flash the firmware - Recommended and most straight forward)</summary>
</details>

<details>
<summary>2. arduino-cli (Just want to flash the firmware - But from your local machine)</summary>

### 2.1. Install Arduino CLI

Follow the [Instructions from here](https://arduino.github.io/arduino-cli/1.1/installation/) for your platform (Pick the latest version from the top left drop down).

### 2.2. Install ESP32 boards

```bash
# Add the ESP32 boards URL
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Update the core index
arduino-cli core update-index

# Install the ESP32 core
arduino-cli core install esp32:esp32
```

### 2.3. Building and uploading the firmware

#### 2.3.1. Clone this repository to your local machine.

#### 2.3.2. Navigate to the [main/arduino/button_firmware](main/arduino/button_firmware) directory.

```bash
cd main/arduino/button_firmware
```

#### 2.3.3. Clean and create a new binary directory

```bash
rm -rf binary
mkdir -p binary
```

#### 2.3.4. Compile the firmware

```bash
arduino-cli compile -v --fqbn esp32:esp32:esp32h2:UploadSpeed=921600,CDCOnBoot=default,FlashFreq=64,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default --output-dir binary .

# verify
cd binary && ls -la
```

#### 2.3.5. Upload the firmware

```bash
# Assuming you are in main/arduino/button_firmware 
# and a binary dir exists with the compiled binaries from the previous step
arduino-cli upload -v --fqbn esp32:esp32:esp32h2:UploadSpeed=921600,CDCOnBoot=default,FlashFreq=64,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default --port /dev/your-serial-port binary/button_firmware.ino.merged.bin
```

> `--port /dev/your-serial-port`: Specifies the serial port to which the ESP32-H2 board is connected.
>
> Replace `/dev/your-serial-port` with the actual serial port name on your system (e.g., `/dev/ttyUSB0` on Linux, `COM3` on Windows).
>
> You can find the port na