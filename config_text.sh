#!/bin/bash

# BPI Router Linux Configuration Script (Text-based)
# This script creates build.conf from build.tmp template using text interface

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
DEFAULT_UPLOAD_USER="$USER"
DEFAULT_UPLOAD_SERVER="r3"
DEFAULT_UPLOAD_DIR="/var/lib/tftp"
DEFAULT_BOARD_TYPE="bpi-r4"
DEFAULT_OWN_MODULES="mt76\|bluetooth"

echo -e "${BLUE}BPI Router Linux Configuration${NC}"
echo "=================================="
echo

# Function to get user input with default
get_input() {
    local prompt="$1"
    local default="$2"
    local value
    
    read -p "$prompt [$default]: " value
    echo "${value:-$default}"
}

echo -e "${YELLOW}Upload Settings:${NC}"
echo "Default values:"
echo "  Upload User: \$USER"
echo "  Upload Server: r3"
echo "  Upload Directory: /var/lib/tftp"
echo
read -p "Do you want to configure upload settings? [y/N]: " configure_upload

if [[ $configure_upload =~ ^[Yy]$ ]]; then
    UPLOAD_USER=$(get_input "Upload User" "$DEFAULT_UPLOAD_USER")
    UPLOAD_SERVER=$(get_input "Upload Server" "$DEFAULT_UPLOAD_SERVER")
    UPLOAD_DIR=$(get_input "Upload Directory" "$DEFAULT_UPLOAD_DIR")
else
    UPLOAD_USER="\$USER"
    UPLOAD_SERVER="r3"
    UPLOAD_DIR="/var/lib/tftp"
fi

echo
echo -e "${YELLOW}Build Settings:${NC}"
echo "Build Directory: ../build (fixed default)"
echo "RAM Disk Size: 8G (fixed default)"

echo
echo -e "${YELLOW}Board Selection:${NC}"
echo "Available boards:"
echo "1) bpi-r2"
echo "2) bpi-r64"
echo "3) bpi-r2pro"
echo "4) bpi-r3"
echo "5) bpi-r3mini"
echo "6) bpi-r4 (Default)"
echo "7) bpi-r4pro"
echo "8) bpi-r4lite"

read -p "Select board [6]: " board_choice
case ${board_choice:-6} in
    1) BOARD_TYPE="bpi-r2" ;;
    2) BOARD_TYPE="bpi-r64" ;;
    3) BOARD_TYPE="bpi-r2pro" ;;
    4) BOARD_TYPE="bpi-r3" ;;
    5) BOARD_TYPE="bpi-r3mini" ;;
    6) BOARD_TYPE="bpi-r4" ;;
    7) BOARD_TYPE="bpi-r4pro" ;;
    8) BOARD_TYPE="bpi-r4lite" ;;
    *) BOARD_TYPE="bpi-r4" ;;
esac

echo
echo -e "${YELLOW}Advanced Options:${NC}"
OWN_MODULES=$(get_input "Own Modules (grep pattern)" "$DEFAULT_OWN_MODULES")

echo
echo -e "${BLUE}Configuration Summary:${NC}"
echo "Upload User: $UPLOAD_USER"
echo "Upload Server: $UPLOAD_SERVER"
echo "Upload Directory: $UPLOAD_DIR"
echo "Build Directory: ../build (fixed default)"
echo "RAM Disk Size: 8G (fixed default)"
echo "Board Type: $BOARD_TYPE"
echo "Own Modules: $OWN_MODULES"

echo
read -p "Generate build.conf with these settings? [y/N]: " confirm
if [[ $confirm =~ ^[Yy]$ ]]; then
    if [ ! -f "build.tmp" ]; then
        echo -e "${RED}Error: build.tmp template file not found!${NC}"
        exit 1
    fi
    
    # Create backup of existing build.conf if it exists
    if [ -f "build.conf" ]; then
        backup_name="build.conf.bak.$(date +%Y%m%d_%H%M%S)"
        cp build.conf "$backup_name"
        echo -e "${GREEN}Backup of existing build.conf created: $backup_name${NC}"
    fi
    
    # Generate new build.conf from template
    sed -e "s|\$USER|$UPLOAD_USER|g" \
        -e "s|r3|$UPLOAD_SERVER|g" \
        -e "s|/var/lib/tftp|$UPLOAD_DIR|g" \
        -e "s|\${OWN_MODULES}|$OWN_MODULES|g" \
        build.tmp > build.conf
    
    echo -e "${GREEN}Configuration generated successfully!${NC}"
    echo "File: build.conf"
    echo "Board: $BOARD_TYPE"
else
    echo "Configuration cancelled."
fi
