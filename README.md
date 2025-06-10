# Pregometer - Pregnancy Progress Tracker

A pregnancy progress tracker that runs on the Inkplate2, an ESP32 connected to a 2" tri-color (black, white, red) eInk display.

## Features

- **Daily Updates**: Automatically wakes up at 7:00 AM every day to update the display
- **Pregnancy Progress**: Shows days remaining until due date, current week, and trimester
- **Progress Bar**: Visual progress indicator with percentage complete
- **Low Power**: Uses ESP32 deep sleep between updates for extended battery life
- **WiFi Time Sync**: Automatically synchronizes time using NTP servers

## Display Layout

The 212x104 pixel landscape display shows:
- **Left side**: Large days remaining number with "days left" below
- **Right side**: Current week number with trimester information
- **Middle**: Progress bar (180px × 20px) with percentage inside
- **Bottom left**: Last update timestamp in MM/DD HH:MM format

## Hardware Requirements

- Inkplate2 (ESP32 with 2" tri-color eInk display)
- WiFi network for time synchronization

## Setup

1. **Install Libraries**:
   - Inkplate board definitions
   - Inkplate library for Arduino IDE

2. **Configure WiFi**:
   ```cpp
   char ssid[] = "YourWiFiNetwork";
   char pass[] = "YourWiFiPassword";
   ```

3. **Set Timezone**:
   ```cpp
   int timeZone = -4; // Adjust for your timezone (hours from UTC)
   ```

4. **Configure Pregnancy Dates**:
   ```cpp
   // Due date (year, month-1, day)
   dueDate.tm_year = 2024 - 1900;
   dueDate.tm_mon = 10 - 1;
   dueDate.tm_mday = 0;
   
   // Start date - Last Menstrual Period (LMP)
   startDate.tm_year = 2023 - 1900;
   startDate.tm_mon = 12 - 1;
   startDate.tm_mday = 25;
   ```

## File Structure

```
pregometer/
├── pregometer.ino      # Main Arduino sketch
├── Network.h/cpp       # WiFi and NTP time synchronization
├── RTC.h/cpp          # Real-time clock and deep sleep functionality
├── CLAUDE.md          # Project documentation
└── examples/          # Manufacturer example sketches
```

## How It Works

1. **First Boot**: Device connects to WiFi, synchronizes time, displays pregnancy progress, and sets up daily 7 AM alarm
2. **Daily Wake-up**: Device wakes from deep sleep, updates time via WiFi, refreshes display, sets next alarm, and returns to sleep
3. **Calculations**:
   - **Days remaining**: Calculated from current date to due date
   - **Current week**: Based on days since LMP start date ÷ 7
   - **Trimester**: Week 1-12 (1st), 13-27 (2nd), 28+ (3rd)
   - **Progress**: Days passed ÷ 280 total pregnancy days × 100%

## Technical Notes

- Pregnancy duration: 280 days (40 weeks)
- No battery monitoring (hardware limitation)
- Device shows progress on every boot/reboot, not just scheduled wake-ups
- Uses ESP32 deep sleep timer for wake-up scheduling

## License

Based on Inkplate library examples released under GNU Lesser General Public License v3.0.

## Support

For Inkplate hardware support: [forum.e-radionica.com/en](https://forum.e-radionica.com/en)  
For product information: [www.inkplate.io](https://www.inkplate.io)
