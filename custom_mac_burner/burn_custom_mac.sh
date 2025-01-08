#!/bin/bash

# Define colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Config file for last used port
LAST_PORT_FILE=".last_port"

# Function to check if arduino-cli is installed
check_arduino_cli() {
    echo -e "${YELLOW}[*]${NC} Checking arduino-cli..."
    if ! command -v arduino-cli > /dev/null 2>&1; then
        echo -e "${RED}[!]${NC} arduino-cli not found. Please install it first."
        echo -e "    Visit: https://arduino.github.io/arduino-cli/latest/installation/"
        exit 1
    fi
    echo -e "${GREEN}[✓]${NC} arduino-cli found"
}

# Function to check for required tools
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
    echo -e "${GREEN}[✓]${NC} All required tools found"
}

# Function to validate MAC address format
validate_mac() {
    local mac=$1
    if [[ ! $mac =~ ^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ ]]; then
        return 1
    fi
    
    # Check for correct prefix
    local prefix=$(echo $mac | cut -d':' -f1-3)
    if [[ "${prefix,,}" != "60:55:f9" ]]; then
        echo -e "${RED}[!]${NC} Invalid manufacturer prefix. Must start with 60:55:F9"
        return 1
    fi
    
    return 0
}

# Function to save last used port
save_port() {
    echo "$1" > "$LAST_PORT_FILE"
}

# Function to get last used port
get_last_port() {
    if [ -f "$LAST_PORT_FILE" ]; then
        cat "$LAST_PORT_FILE"
    fi
}

# Function to generate unicast MAC address
generate_unicast_mac() {
    # Start with manufacturer prefix
    local prefix="00:60:2F"
    
    # Generate random bytes for the rest
    local byte4=$(printf "%02X" $((RANDOM % 256)))
    local byte5=$(printf "%02X" $((RANDOM % 256)))
    local byte6=$(printf "%02X" $((RANDOM % 256)))
    
    # Ensure first byte is even (unicast)
    local first_digit=$(printf "%d" "0x${byte4:0:1}")
    if [ $((first_digit % 2)) -eq 1 ]; then
        byte4=$(printf "%02X" $((0x$byte4 - 1)))
    fi
    
    echo "$prefix:$byte4:$byte5:$byte6"
}

# Function to find ESP32-H2 port
find_port() {
    local last_port=$(get_last_port)
    if [ -n "$last_port" ] && [ -e "$last_port" ]; then
        echo "$last_port"
        return
    fi
    
    local port=$(ls /dev/cu.usbserial-* 2>/dev/null | head -n 1)
    if [ -z "$port" ]; then
        echo -e "${RED}[!]${NC} No ESP32-H2 device found. Please:"
        echo -e "    1. Connect your ESP32-H2 device, or"
        echo -e "    2. Provide the port as an argument: $0 /dev/cu.usbserial-XXXX"
        exit 1
    fi
    
    echo "$port"
}


# Function to burn custom MAC (modified to use clean PORT)
burn_custom_mac() {
    local mac=$1
    local port=$2
    local command="espefuse.py --chip esp32h2 --port $port burn_custom_mac $mac"
    
    echo -e "\n${YELLOW}[!] WARNING:${NC}"
    echo -e "    You are about to burn a custom MAC address to your ESP32-H2."
    echo -e "    This operation is ${RED}IRREVERSIBLE${NC}!"
    echo -e "    Port: $port"
    echo -e "    MAC:  $mac"
    
    echo -e "\n${YELLOW}[*]${NC} Command to execute:"
    echo -e "    $command"
    
    echo -e "\nAre you sure you want to continue? (y/N): "
    read -r confirm
    
    if [[ ! $confirm =~ ^[Yy]$ ]]; then
        echo -e "${YELLOW}[*]${NC} Operation cancelled by user"
        exit 0
    fi
    
    echo -e "\n${YELLOW}[*]${NC} Burning custom MAC: $mac"
    eval "$command"
    if [ $? -ne 0 ]; then
        echo -e "${RED}[!]${NC} Failed to burn custom MAC"
        # exit 1
    fi
}


# Function to verify custom MAC
verify_custom_mac() {
    local port=$1
    
    echo -e "${YELLOW}[*]${NC} Verifying custom MAC..."
    espefuse.py --chip esp32h2 --port "$port" get_custom_mac
    if [ $? -ne 0 ]; then
        echo -e "${RED}[!]${NC} Failed to verify custom MAC"
        exit 1
    fi
}

# Show usage
show_usage() {
    echo "ESP32-H2 eFuse Custom MAC Burner Script"
    echo "======================================="
    echo "Usage: $0 [--port <PORT>] [--help]"
    echo ""
    echo "Options:"
    echo "  --port <PORT>    Specify the serial port (e.g., /dev/cu.usbserial-2120)"
    echo "  --help          Show this help message"
    echo ""
    echo "If --port is not provided, will try to use last used port or auto-detect."
    echo ""
    echo "Examples:"
    echo "  $0 --port /dev/cu.usbserial-2120    # Use specific port"
    echo "  $0                                   # Auto-detect port"
    echo "  $0 --help                           # Show this help"
    exit 1
}

# Main execution
clear
echo -e "=== ESP32-H2 eFuse Custom MAC Burner Script ==="
echo -e "================================================"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --help)
            show_usage
            ;;
        --port)
            if [ -n "$2" ]; then
                if [ -e "$2" ]; then
                    PORT="$2"
                    save_port "$PORT"
                    echo -e "${GREEN}[✓]${NC} Using provided port: $PORT"
                else
                    echo -e "${RED}[!]${NC} Error: Specified port $2 does not exist"
                    show_usage
                fi
                shift 2
            else
                echo -e "${RED}[!]${NC} Error: --port requires a port argument"
                show_usage
            fi
            ;;
        *)
            echo -e "${RED}[!]${NC} Error: Unknown argument: $1"
            show_usage
            ;;
    esac
done


# If no port specified, try auto-detection
if [ -z "$PORT" ]; then
    echo -e "${YELLOW}[*]${NC} No port specified, looking for ESP32-H2 device..."
    PORT=$(find_port)
    if [ -n "$PORT" ]; then
        echo -e "${GREEN}[✓]${NC} Found device at: $PORT"
    fi
fi


# Check requirements
check_arduino_cli
sleep 1
check_tools
sleep 1


# Generate and validate MAC
while true; do
    echo -e "\nDo you want to:"
    echo -e "1. Generate a random unicast MAC"
    echo -e "2. Enter a custom MAC manually"
    echo -e "Choose (1/2): "
    read -r choice
    
    case $choice in
        1)
            MAC_ADDRESS=$(generate_unicast_mac)
            echo -e "Generated MAC: $MAC_ADDRESS"
            ;;
        2)
            echo -e "Enter custom MAC address (format: 60:55:F9:XX:XX:XX):"
            read -r MAC_ADDRESS
            ;;
        *)
            echo -e "${RED}[!]${NC} Invalid choice"
            continue
            ;;
    esac
    
    if validate_mac "$MAC_ADDRESS"; then
        break
    else
        echo -e "${RED}[!]${NC} Invalid MAC address format. Please use format: 60:55:F9:XX:XX:XX"
    fi
done

# Burn and verify
echo -e "\n${YELLOW}[*]${NC} Starting MAC address burning process..."
burn_custom_mac "$MAC_ADDRESS" "$PORT"
sleep 1
verify_custom_mac "$PORT"

echo -e "\n${GREEN}[✓]${NC} Process completed successfully!"