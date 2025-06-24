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
- pregometer.ino - Main sketch file
- Network.h/cpp - WiFi and NTP time synchronization
- RTC.h/cpp - Real-time clock and deep sleep alarm functionality

## Configuration needed:
- WiFi credentials in ssid[] and pass[] variables
- Pregnancy dates in dueDate and startDate structs
- Timezone in timeZone variable

## Hardware notes:
- No battery gauge available on this Inkplate2 hardware
- Device shows pregnancy progress on every boot/reboot, not just alarm wake-ups
- Week calculation: based on days since LMP start date divided by 7 (no +1 offset)
- Pregnancy duration: 280 days total (40 weeks)

## Compile and Upload

You can use the arduino-cli tool to compile, add libraries and upload new firmware.

Compile:

    arduino-cli compile --fqbn Inkplate_Boards:esp32:Inkplate2

Upload:

   arduino-cli upload -p /dev/ttyUSB0 --fqbn Inkplate_Boards:esp32:Inkplate2 
