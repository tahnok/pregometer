This is an Arduino sketch that runs on the Inkplate2, an ESP32 connected to a three colour (black,white,red) 2" eInk display.

The screen is 212 x 104 px and the default orientation is landscape.

Once a day at 7am the device wakes up and updates the screen. It then goes back to sleep until the next day.
The deep sleep clock uses the RTC periphreal and sets the time using WiFI when it wakes up.

The purpose of this app is show the progress of a pregnancy. 

The screen shows the number of days until the due date, the current trimester and the current week of the pregnancy.
There's also a percentage complete based on the total number of days of the pregancy, and a progress bar.
The last update date/time is displayed in the bottom left corner in MM/DD HH:MM format.

The examples/ directory contains example sketches from the manufacturer showing how the RTC works and WiFi.

## Display Layout (212x104 landscape):
- Left side: Large days remaining number (text size 3) with "days left" below
- Right side: Week number (text size 2) with "Trimester X" below  
- Middle: Wide progress bar (180px x 20px) with percentage inside
- Bottom left: Last update timestamp

## Project Structure:
- pregometer.ino - Main sketch file with WiFiManager integration
- Network.h/cpp - WiFi and NTP time synchronization
- RTC.h/cpp - Real-time clock and deep sleep alarm functionality

## Dependencies:
- WiFiManager library (tzapu/WiFiManager)
- ArduinoJson library for configuration storage
- SPIFFS for persistent configuration storage

## Configuration:
The device uses WiFiManager for configuration. On first boot or when pregnancy dates are not configured:

1. Device creates "Pregometer-Setup" WiFi access point
2. Connect to this WiFi and navigate to 192.168.4.1
3. Configure WiFi credentials and pregnancy dates:
   - Start Date: LMP date in YYYY-MM-DD format
   - Due Date: Expected due date in YYYY-MM-DD format
4. Configuration is saved to SPIFFS and persists across reboots

### Reconfiguring Dates:
To change the due date or start date after initial configuration:

1. Connect to the device via serial monitor at 115200 baud
2. Press the reset button or power cycle the device
3. Within 3 seconds of boot, type "RECONFIGURE" and press Enter
4. The device will enter configuration mode and create the "Pregometer-Setup" WiFi access point
5. Connect to the WiFi and navigate to 192.168.4.1 to update dates

Manual configuration in code (if needed):
- Timezone in timeZone variable (default: -3 for UTC-3)

## Hardware notes:
- No battery gauge available on this Inkplate2 hardware
- Device shows pregnancy progress on every boot/reboot, not just alarm wake-ups
- Week calculation: based on days since LMP start date divided by 7 (no +1 offset)
- Pregnancy duration: 280 days total (40 weeks)

## Compile and Upload

You can use the arduino-cli tool to compile, add libraries and upload new firmware.

Install required libraries:

    arduino-cli lib install WiFiManager ArduinoJson

Compile:

    arduino-cli compile --fqbn Inkplate_Boards:esp32:Inkplate2

Upload:

    arduino-cli upload -p /dev/ttyUSB0 --fqbn Inkplate_Boards:esp32:Inkplate2

Monitor serial output:

    arduino-cli monitor -p /dev/ttyUSB0 --fqbn Inkplate_Boards:esp32:Inkplate2 --config baudrate=115200 
