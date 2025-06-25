/*
    Pregometer - Pregnancy Progress Tracker for Inkplate 2
    
    This Arduino sketch runs on the Inkplate2, an ESP32 connected to a three color 
    (black, white, red) 2" eInk display.
    
    Once a day at 7am the device wakes up and updates the screen showing:
    - Days until due date
    - Current trimester 
    - Current week of pregnancy
    - Percentage complete and progress bar
    - Last update date/time
    
    Then goes back to deep sleep until the next day.
*/

#ifndef ARDUINO_INKPLATE2
#error "Wrong board selection for this example, please select Soldered Inkplate2 in the boards menu."
#endif

#include "Inkplate.h"
#include "Network.h"
#include "RTC.h"
#include <time.h>
#include "FreeSans9pt7b.h"
#include "FreeSans12pt7b.h"
#include "FreeSans18pt7b.h"
#include "FreeSansBold24pt7b.h"

Inkplate display;
Network network; 
RTC rtc;

// WiFi credentials - update these
char ssid[] = "Placeholder";
char pass[] = "daylilies2024";

// Timezone adjustment (1 means UTC+1)
int timeZone = -3;

// Pregnancy configuration - update these dates
struct tm dueDate = {0};      // Due date
struct tm startDate = {0};    // Pregnancy start date (LMP)

// Wake up time (7:00 AM)
const int WAKE_HOUR = 7;
const int WAKE_MINUTE = 0;

// No battery gauge on this hardware

// Total pregnancy duration in days (280 days = 40 weeks)
const int TOTAL_PREGNANCY_DAYS = 280;

struct tm currentTime;

void setup() {
    Serial.begin(115200);
    
    // Initialize display
    display.begin();
    display.clearDisplay();
    
    // Set pregnancy dates - UPDATE THESE VALUES
    // Due date example: March 15, 2025
    dueDate.tm_year = 2025 - 1900;  // Year since 1900
    dueDate.tm_mon = 12 - 1;         // Month (0-11)
    dueDate.tm_mday = 2;           // Day of month
    
    // Start date example: June 8, 2024 (LMP)
    startDate.tm_year = 2025 - 1900;
    startDate.tm_mon = 2 - 1;
    startDate.tm_mday = 25;
    
    // Check why device woke up
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
        // First run - set up alarm for daily wake up
        setupDailyAlarm();
                updatePregnancyDisplay();

    } else {
        // Daily wake up - update display
        updatePregnancyDisplay();
    }
}

void loop() {
    // Empty - everything happens in setup() due to deep sleep
}

void setupDailyAlarm() {
    // Connect to WiFi and get current time
    network.begin(ssid, pass);
    network.setTime(timeZone);
    network.getTime(&currentTime);
    
    // Set timezone for RTC
    rtc.setTimezone(timeZone);
    
    // Set alarm for next 7:00 AM
    struct tm alarmTime = {0};
    alarmTime.tm_hour = WAKE_HOUR;
    alarmTime.tm_min = WAKE_MINUTE;
    alarmTime.tm_sec = 0;
    
    // If it's already past 7 AM today, set for tomorrow
    if (currentTime.tm_hour > WAKE_HOUR || 
        (currentTime.tm_hour == WAKE_HOUR && currentTime.tm_min >= WAKE_MINUTE)) {
        alarmTime.tm_mday = currentTime.tm_mday + 1;
        alarmTime.tm_mon = currentTime.tm_mon;
    } else {
        alarmTime.tm_mday = currentTime.tm_mday;
        alarmTime.tm_mon = currentTime.tm_mon;
    }
    
    double secondsUntilAlarm = rtc.setAlarm(alarmTime, RTC_DHHMMSS);
    
    if (secondsUntilAlarm > 0) {
        // Show current pregnancy progress
        showPregnancyProgress();
        deepSleep();
    } else {
        display.setTextColor(INKPLATE2_BLACK);
        display.setCursor(10, 20);
        display.setTextSize(1);
        display.println("Error: Could not set alarm");
        display.display();
        deepSleep();
    }
}

void updatePregnancyDisplay() {
    // Get current time
    network.begin(ssid, pass);
    network.setTime(timeZone);
    network.getTime(&currentTime);
    
    // Show pregnancy progress
    showPregnancyProgress();
    
    // Set next alarm for tomorrow at 7 AM
    setNextDailyAlarm();
    
    // Go back to sleep
    deepSleep();
}

void showPregnancyProgress() {
    // Calculate pregnancy progress
    int daysRemaining = calculateDaysRemaining();
    int currentWeek = calculateCurrentWeek();
    int currentTrimester = calculateTrimester(currentWeek);
    float percentComplete = calculatePercentComplete();
    
    // Clear display
    display.clearDisplay();
    display.setTextColor(INKPLATE2_BLACK);
    
    // Display pregnancy information
    displayPregnancyInfo(daysRemaining, currentWeek, currentTrimester, percentComplete);
    
    // Display progress bar
    displayProgressBar(percentComplete);
    
    // Display last update time
    displayLastUpdate();
    
    // Update display
    display.display();
}

void displayPregnancyInfo(int daysRemaining, int currentWeek, int currentTrimester, float percentComplete) {
    // Left column - Days remaining (large with bold font)
    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(5, 39);
    display.printf("%d", daysRemaining);
    
    // Center "days left" under the number
    display.setFont(&FreeSans9pt7b);
    display.setCursor(15, 54);
    display.println("days left");
    
    // Right column - Week info (moved left to avoid clipping)
    display.setFont(&FreeSans12pt7b);
    display.setCursor(100, 25);
    display.printf("Week %d", currentWeek);
    
    // Trimester info
    display.setFont(&FreeSans9pt7b);
    display.setCursor(100, 40);
    display.printf("Trimester %d", currentTrimester);
    
}

void displayProgressBar(float percentComplete) {
    int barWidth = 198;  // Fill almost the entire screen width (212px - 14px margins)
    int barHeight = 30;  // Make it taller
    int barX = 5;
    int barY = 70;       // Move it down
    
    // Draw progress bar outline
    display.drawRect(barX, barY, barWidth, barHeight, INKPLATE2_BLACK);
    
    // Fill progress bar
    int fillWidth = (int)((barWidth - 2) * percentComplete / 100.0);
    if (fillWidth > 0) {
        display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, INKPLATE2_BLACK);
    }
    
    // Add percentage text inside bar - position and color based on fill level
    display.setFont(&FreeSans9pt7b);
    if (percentComplete < 50.0) {
        // Less than 50% filled - use black text, right-aligned in unfilled area
        display.setTextColor(INKPLATE2_BLACK);
        display.setCursor(barX + barWidth - 40, barY + 20);  // Right side of bar
    } else {
        // More than 50% filled - use white text, left-aligned in filled area
        display.setTextColor(INKPLATE2_WHITE);
        display.setCursor(barX + 10, barY + 20);  // Left side of bar
    }
    display.printf("%.0f%%", percentComplete);
}

void displayLastUpdate() {
    // This function is now unused - timestamp moved to displayPregnancyInfo
}

int calculateDaysRemaining() {
    time_t now = mktime(&currentTime);
    time_t due = mktime(&dueDate);
    
    double diffSeconds = difftime(due, now);
    int diffDays = (int)(diffSeconds / (24 * 3600));
    
    return diffDays > 0 ? diffDays : 0;
}

int calculateCurrentWeek() {
    time_t now = mktime(&currentTime);
    time_t start = mktime(&startDate);
    
    double diffSeconds = difftime(now, start);
    int diffDays = (int)(diffSeconds / (24 * 3600));
    
    int week = (diffDays / 7);
    return week > 0 ? (week > 40 ? 40 : week) : 1;
}

int calculateTrimester(int week) {
    if (week <= 12) return 1;
    else if (week <= 27) return 2;
    else return 3;
}

float calculatePercentComplete() {
    time_t now = mktime(&currentTime);
    time_t start = mktime(&startDate);
    
    double diffSeconds = difftime(now, start);
    int daysPassed = (int)(diffSeconds / (24 * 3600));
    
    float percent = ((float)daysPassed / TOTAL_PREGNANCY_DAYS) * 100.0;
    return percent > 100.0 ? 100.0 : (percent < 0.0 ? 0.0 : percent);
}

// Battery gauge removed - no hardware support

void setNextDailyAlarm() {
    struct tm alarmTime = {0};
    alarmTime.tm_hour = WAKE_HOUR;
    alarmTime.tm_min = WAKE_MINUTE;
    alarmTime.tm_sec = 0;
    alarmTime.tm_mday = currentTime.tm_mday + 1;
    alarmTime.tm_mon = currentTime.tm_mon;
    
    rtc.setAlarm(alarmTime, RTC_DHHMMSS);
}

void deepSleep() {
    esp_deep_sleep_start();
}