# ESP32-H2 Custom MAC Address Burner

This script automates the process of burning custom MAC addresses to ESP32-H2 devices using the ESP efuse utility. It provides both automated and manual MAC address generation options while ensuring proper unicast address formatting.

> ⚠️ WARNING
>
> This operation is IRREVERSIBLE!
>
> Once a custom MAC address is burned to the ESP32-H2, it cannot be changed.

## Prerequisites

- arduino-cli installed (Installation Guide)
- Python and pip installed
- ESP tools (will be automatically installed if teh Arduino Core for esp-32 was installed if missing)
  - esptool.py
  - espsecure.py
  - espefuse.py

## Features

- Automatic port detection for ESP32-H2 devices
- MAC address validation
- Random unicast MAC address generation
- Custom MAC address input option
- Persistence of last used port
- Safety confirmations before burning
- Verification after burning

## Usage

```bash
bashCopy./esp32h2_mac_burner.sh [--port <PORT>] [--help]

# Options
--port <PORT>: Specify the serial port (e.g., /dev/cu.usbserial-2120)

--help: Show help message
```

> If no port is specified, the script will attempt to use the last used port or auto-detect the device.

## MAC Address Format

The MAC address must follow the format: `60:55:F9:XX:XX:XX` where:

- `60:55:F9` is the manufacturer prefix (specific to ESP32-H2)
XX:XX:XX are hexadecimal values. From Espressif's official documentation, the ESP32-H2 family (being a newer product) primarily uses the following MAC OUI prefix: `60:55:F9`. It will thus:
  - Comply with IEEE MAC address assignments.
  - Ensure compatibility with Espressif's device management tools.
  - Prevent potential conflicts with devices from other manufacturers.
- Also, the first byte must be even (for unicast addresses)

## Security Considerations

- Only unicast addresses are generated to prevent network conflicts.
- The script performs validation to ensure MAC addresses are properly formatted.
- Confirmation is required before burning to prevent accidental operations.
- __The operation is irreversible - use with caution__

## Error Handling

The script includes error checking for:

- Missing prerequisites
- Invalid port specification
- Incorrect MAC address format
- Failed burning operations
- Failed verification
