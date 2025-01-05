#!/bin/bash

# Define colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Add this to check if arduino-cli is installed
check_arduino_cli() {
    if ! command -v arduino-cli > /dev/null 2>&1; then
        echo -e "${RED}[!]${NC} arduino-cli not found. Please install it first."
        echo -e "    Visit: https://arduino.github.io/arduino-cli/latest/installation/"
        exit 1
    fi
}

# Check for required tools
check_tools() {
    echo -e "${YELLOW}[*]${NC} Checking required tools..."
    
    # Check pip
    if ! command -v pip > /dev/null 2>&1; then
        echo -e "${RED}[!]${NC} pip not found. Please install Python and pip first."
        exit 1
    fi
    
    # Check/install ESP tools
    for tool in esptool.py espsecure.py espefuse.py; do
        if ! command -v "$tool" > /dev/null 2>&1; then
            echo -e "${RED}[!]${NC} $tool not found. Installing..."
            pip install esptool
            break
        fi
    done
}

# espefuse.py --chip esp32h2 --port /dev/cu.usbserial-2120 burn_custom_mac 00:60:2F:15:71:61
# espefuse.py --chip esp32h2 --port /dev/cu.usbserial-2120 get_custom_mac

# Main execution
clear
echo -e "=== ESP32-H2 eFuse custom unique MAC burner Script ==="

check_arduino_cli
sleep 1
check_tools
sleep 1
# 

echo -e "${GREEN}[✓]${NC} Process completed successfully!"
