#!/bin/bash

# BPI Router Linux Configuration Dialog
# This script creates build.conf from build.tmp template using dialog interface

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

# Check if dialog is installed
if ! command -v dialog &> /dev/null; then
    echo -e "${RED}Error: dialog is not installed. Please install it first:${NC}"
    echo "sudo apt-get install dialog  # Debian/Ubuntu"
    echo "sudo yum install dialog      # CentOS/RHEL"
    echo "sudo pacman -S dialog        # Arch Linux"
    exit 1
fi

# Function to show main menu
show_main_menu() {
    dialog --backtitle "BPI Router Linux Configuration" \
           --title "Main Menu" \
           --menu "Select configuration option:" \
           15 60 8 \
           1 "Upload Settings" \
           2 "Build Settings" \
           3 "Board Selection" \
           4 "Advanced Options" \
           5 "Generate Configuration" \
           6 "Exit" 2>/tmp/dialog_result
}

# Function to configure upload settings
configure_upload() {
    dialog --backtitle "BPI Router Linux Configuration" \
           --title "Upload Settings" \
           --yesno "Current upload settings:\n\nUser: \$USER\nServer: r3\nDirectory: /var/lib/tftp\n\nDo you want to change these settings?" 10 50
    
    if [ $? -eq 0 ]; then
        dialog --backtitle "BPI Router Linux Configuration" \
               --title "Upload Settings" \
               --form "Configure upload parameters:" \
               15 60 0 \
               "Upload User:" 1 1 "$DEFAULT_UPLOAD_USER" 1 20 20 0 \
               "Upload Server:" 2 1 "$DEFAULT_UPLOAD_SERVER" 2 20 20 0 \
               "Upload Directory:" 3 1 "$DEFAULT_UPLOAD_DIR" 3 20 40 0 2>/tmp/dialog_result
        
        if [ $? -eq 0 ]; then
            readarray -t upload_values < /tmp/dialog_result
            DEFAULT_UPLOAD_USER="${upload_values[0]}"
            DEFAULT_UPLOAD_SERVER="${upload_values[1]}"
            DEFAULT_UPLOAD_DIR="${upload_values[2]}"
            dialog --msgbox "Upload settings updated!" 6 40
        fi
    else
        # Keep defaults
        DEFAULT_UPLOAD_USER="\$USER"
        DEFAULT_UPLOAD_SERVER="r3"
        DEFAULT_UPLOAD_DIR="/var/lib/tftp"
        dialog --msgbox "Upload settings kept as defaults." 6 40
    fi
}

# Function to configure build settings
configure_build() {
    dialog --backtitle "BPI Router Linux Configuration" \
           --title "Build Settings" \
           --msgbox "Build settings are fixed to defaults:\n\nBuild Directory: ../build\nRAM Disk Size: 8G\n\nThese values are hardcoded and cannot be changed through this interface." 10 50
}

# Function to configure board selection
configure_board() {
    dialog --backtitle "BPI Router Linux Configuration" \
           --title "Board Selection" \
           --radiolist "Select target board:" \
           15 60 8 \
           0 "bpi-r2" off \
           1 "bpi-r64" off \
           2 "bpi-r2pro" off \
           3 "bpi-r3" off \
           4 "bpi-r3mini" off \
           5 "bpi-r4 (Default)" on \
           6 "bpi-r4pro" off \
           7 "bpi-r4lite" off 2>/tmp/dialog_result
    
    if [ $? -eq 0 ]; then
        selection=$(cat /tmp/dialog_result)
        case $selection in
            0) DEFAULT_BOARD_TYPE="bpi-r4" ;;
            1) DEFAULT_BOARD_TYPE="bpi-r64" ;;
            2) DEFAULT_BOARD_TYPE="bpi-r2pro" ;;
            3) DEFAULT_BOARD_TYPE="bpi-r3" ;;
            4) DEFAULT_BOARD_TYPE="bpi-r3mini" ;;
            5) DEFAULT_BOARD_TYPE="bpi-r4" ;;
            6) DEFAULT_BOARD_TYPE="bpi-r4pro" ;;
            7) DEFAULT_BOARD_TYPE="bpi-r4lite" ;;
        esac
        dialog --msgbox "Board selection updated to: $DEFAULT_BOARD_TYPE" 6 50
    fi
}

# Function to configure advanced options
configure_advanced() {
    dialog --backtitle "BPI Router Linux Configuration" \
           --title "Advanced Options" \
           --form "Configure advanced parameters:" \
           15 60 0 \
           "Own Modules (grep pattern):" 1 1 "$DEFAULT_OWN_MODULES" 1 30 30 0 2>/tmp/dialog_result
    
    if [ $? -eq 0 ]; then
        readarray -t advanced_values < /tmp/dialog_result
        DEFAULT_OWN_MODULES="${advanced_values[0]}"
        dialog --msgbox "Advanced options updated!" 6 40
    fi
}

# Function to generate build.conf from template
generate_config() {
    if [ ! -f "build.tmp" ]; then
        dialog --msgbox "Error: build.tmp template file not found!" 6 50
        return 1
    fi
    
    # Create backup of existing build.conf if it exists
    if [ -f "build.conf" ]; then
        cp build.conf build.conf.bak.$(date +%Y%m%d_%H%M%S)
        dialog --msgbox "Backup of existing build.conf created" 6 40
    fi
    
    # Generate new build.conf from template
    sed -e "s|\$USER|$DEFAULT_UPLOAD_USER|g" \
        -e "s|r3|$DEFAULT_UPLOAD_SERVER|g" \
        -e "s|/var/lib/tftp|$DEFAULT_UPLOAD_DIR|g" \
        -e "s|\${BOARD_TYPE}|$DEFAULT_BOARD_TYPE|g" \
        -e "s|\${OWN_MODULES}|$DEFAULT_OWN_MODULES|g" \
        build.tmp > build.conf
    
    dialog --msgbox "Configuration generated successfully!\n\nFile: build.conf\nBoard: $DEFAULT_BOARD_TYPE" 8 50
}

# Function to show current configuration
show_current_config() {
    if [ -f "build.conf" ]; then
        dialog --backtitle "BPI Router Linux Configuration" \
               --title "Current Configuration" \
               --textbox build.conf 20 70
    else
        dialog --msgbox "No build.conf file found. Generate configuration first." 6 50
    fi
}

# Main loop
while true; do
    show_main_menu
    choice=$(cat /tmp/dialog_result)
    
    case $choice in
        1) configure_upload ;;
        2) configure_build ;;
        3) configure_board ;;
        4) configure_advanced ;;
        5) generate_config ;;
        6) 
            dialog --yesno "Are you sure you want to exit?" 6 30
            if [ $? -eq 0 ]; then
                break
            fi
            ;;
        *) 
            dialog --msgbox "Invalid option!" 6 30
            ;;
    esac
done

# Cleanup
rm -f /tmp/dialog_result

echo -e "${GREEN}Configuration dialog completed.${NC}"
