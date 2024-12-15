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

<details>
<summary>1. From the web (Just want to flash the firmware - Recommended and most straight forward)</summary>

TBD
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
> You can find the port name by running the `arduino-cli board list` command.

#### 2.3.6. Verify the firmware

TBD
</details>


<details>
<summary>3. Arduino IDE 2.3.4 (Easy for Development and want to look under the hood)</summary>

#### 3.1. Prerequisites

1. Install the latest Arduino IDE (version 2.3.4 or above) for your platform.
2. Open the Arduino IDE and navigate to the Board Manager.
3. Search for "esp32" and install the "esp32 by Espressif" (latest).

#### 3.2. Compile & upload

3.2.1. Clone this repository to your local machine.
3.2.2. Open the [button_firmware.ino](main/arduino/button_firmware/button_firmware.ino) file located in the [main/arduino/button_firmware](main) directory.
3.2.3. In the Arduino IDE, select the following board parameters:

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

3.2.4. Select the appropriate USB Serial port for your device (in Win, make sure you ahve "xxx" drtiver installed and then select the right COM port; on mac and linux, you can ignore).
3.2.5. Click the Upload button to flash the firmware. It will compile and upload
</details>

<details>
<summary>4. Platform IO (If you are a pro and are keen in firmware development)</summary>

TBD

</details>

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
