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

#include <WiFiManager.h>
#define FS_NO_GLOBALS
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "Inkplate.h"
#include "Network.h"
#include "RTC.h"
#include "FreeSans9pt7b.h"
#include "FreeSans12pt7b.h"
#include "FreeSans18pt7b.h"
#include "FreeSansBold24pt7b.h"

Inkplate display;
Network network; 
RTC rtc;

// Configuration constants
const int WAKE_HOUR = 7;
const int WAKE_MINUTE = 0;
const int TOTAL_PREGNANCY_DAYS = 280;
const int TIMEZONE_OFFSET = -3;

// Configuration variables
char start_date[11] = "";
char due_date[11] = "";
bool shouldSaveConfig = false;
bool forceReconfigure = false;

// Date structures
struct tm dueDate = {0};
struct tm startDate = {0};
struct tm currentTime;

void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void loadConfig() {
    Serial.println("mounting FS...");
    if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            Serial.println("reading config file");
            fs::File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("opened config file");
                size_t size = configFile.size();
                std::unique_ptr<char[]> buf(new char[size]);
                configFile.readBytes(buf.get(), size);
                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                if (!deserializeError) {
                    Serial.println("parsed json");
                    strcpy(start_date, json["start_date"]);
                    strcpy(due_date, json["due_date"]);
                } else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
    } else {
        Serial.println("failed to mount FS, formatting...");
        if (SPIFFS.format()) {
            Serial.println("SPIFFS formatted successfully");
            if (SPIFFS.begin()) {
                Serial.println("SPIFFS mounted after format");
            } else {
                Serial.println("SPIFFS mount failed even after format");
            }
        } else {
            Serial.println("SPIFFS format failed");
        }
    }
}

void saveConfig() {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["start_date"] = start_date;
    json["due_date"] = due_date;
    
    fs::File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
    } else {
        serializeJson(json, configFile);
        configFile.close();
        Serial.println("config saved");
    }
}

void parseDateString(const char* dateStr, struct tm* date) {
    int year, month, day;
    sscanf(dateStr, "%d-%d-%d", &year, &month, &day);
    date->tm_year = year - 1900;
    date->tm_mon = month - 1;
    date->tm_mday = day;
}

bool areDatesValid() {
    return (strlen(start_date) == 10 && strlen(due_date) == 10 &&
            start_date[4] == '-' && start_date[7] == '-' &&
            due_date[4] == '-' && due_date[7] == '-');
}

bool checkForReconfigureCommand() {
    Serial.println("Send 'RECONFIGURE' within 3 seconds to change dates...");
    unsigned long startTime = millis();
    String input = "";

    while (millis() - startTime < 3000) {
        if (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.equalsIgnoreCase("RECONFIGURE")) {
                    Serial.println("Reconfiguration requested!");
                    return true;
                }
                input = "";
            } else {
                input += c;
            }
        }
        delay(10);
    }
    Serial.println("Continuing with current configuration...");
    return false;
}

void configureWiFiAndDates() {
    // Configure WiFi using WiFiManager with custom parameters
    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    
    // Force configuration portal to open - don't use saved credentials
    wifiManager.resetSettings();
    
    // Set timeout to give user more time to configure
    wifiManager.setConfigPortalTimeout(300); // 5 minutes
    
    // Custom parameters for pregnancy dates
    WiFiManagerParameter custom_start_date("start_date", "Start Date (YYYY-MM-DD)", start_date, 11);
    WiFiManagerParameter custom_due_date("due_date", "Due Date (YYYY-MM-DD)", due_date, 11);
    
    // Add parameters to WiFiManager
    wifiManager.addParameter(&custom_start_date);
    wifiManager.addParameter(&custom_due_date);
    
    Serial.println("Starting WiFi configuration portal...");
    Serial.println("Connect to 'Pregometer-Setup' WiFi and go to 192.168.4.1");
    Serial.println("Enter your WiFi credentials and pregnancy dates");
    
    // Use startConfigPortal instead of autoConnect to force portal mode
    if (!wifiManager.startConfigPortal("Pregometer-Setup")) {
        Serial.println("Failed to connect or configure - will retry");
        return; // Don't restart, just return and try again
    }
    
    Serial.println("WiFi connected");
    
    // Get custom parameters
    strcpy(start_date, custom_start_date.getValue());
    strcpy(due_date, custom_due_date.getValue());
    
    // Log the configured dates
    Serial.print("Start date: ");
    Serial.println(start_date);
    Serial.print("Due date: ");
    Serial.println(due_date);
    
    // Save configuration if changed
    if (shouldSaveConfig) {
        saveConfig();
    }
}

void setup() {
    Serial.begin(115200);
    delay(100); // Give serial time to initialize
    initializeDisplay();

    // Check if user wants to reconfigure
    forceReconfigure = checkForReconfigureCommand();

    if (!ensureDatesConfigured()) {
        Serial.println("Failed to configure dates");
        return;
    }

    parseDateStrings();

    if (isFirstRun()) {
        setupDailyAlarm();
    }

    updatePregnancyDisplay();
}

void loop() {
    // Empty - everything happens in setup() due to deep sleep
}

void setupDailyAlarm() {
    syncTime();
    setupRTC();
    
    struct tm alarmTime = calculateNextAlarmTime();
    double secondsUntilAlarm = rtc.setAlarm(alarmTime, RTC_DHHMMSS);
    
    if (secondsUntilAlarm > 0) {
        showPregnancyProgress();
        deepSleep();
    } else {
        showErrorMessage("Could not set alarm");
        deepSleep();
    }
}

void updatePregnancyDisplay() {
    syncTime();
    showPregnancyProgress();
    setNextDailyAlarm();
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
    
    
    // Update display
    display.display();
}

void displayPregnancyInfo(int daysRemaining, int currentWeek, int currentTrimester, float percentComplete) {
    displayDaysRemaining(daysRemaining);
    displayWeekInfo(currentWeek, currentTrimester);
}

void displayProgressBar(float percentComplete) {
    int barWidth = 198;  // Fill almost the entire screen width (212px - 14px margins)
    int barHeight = 30;  // Make it taller
    int barX = 5;
    int barY = 70;       // Move it down
    
    // Draw progress bar outline
    display.drawRect(barX, barY, barWidth, barHeight, INKPLATE2_BLACK);
    
    // Calculate text dimensions to center it
    display.setFont(&FreeSans9pt7b);
    char percentText[10];
    sprintf(percentText, "%.0f%%", percentComplete);
    
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(percentText, 0, 0, &x1, &y1, &w, &h);
    
    int textX = barX + (barWidth - w) / 2;
    int textY = barY + (barHeight + h) / 2;
    
    // Calculate fill width and determine text overlap
    int fillWidth = (int)((barWidth - 2) * percentComplete / 100.0);
    int fillRight = barX + 1 + fillWidth;
    int textRight = textX + w;
    
    // Fill the progress bar
    if (fillWidth > 0) {
        display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, INKPLATE2_BLACK);
    }
    
    // Now handle the progress bar text based on what portion overlaps
    if (fillWidth == 0) {
        // No fill - just black text on white background
        display.setTextColor(INKPLATE2_BLACK);
        display.setCursor(textX, textY);
        display.print(percentText);
    } else if (textRight <= fillRight) {
        // Text entirely within filled area - white text on black background
        display.setTextColor(INKPLATE2_WHITE);
        display.setCursor(textX, textY);
        display.print(percentText);
    } else if (textX >= fillRight) {
        // Text entirely in unfilled area - black text on white background
        display.setTextColor(INKPLATE2_BLACK);
        display.setCursor(textX, textY);
        display.print(percentText);
    } else {
        // Text spans both areas - draw white text first, then mask unfilled portion
        display.setTextColor(INKPLATE2_WHITE);
        display.setCursor(textX, textY);
        display.print(percentText);
        
        // Clear the unfilled portion back to white background
        display.fillRect(fillRight, barY + 1, (barX + barWidth - 1) - fillRight, barHeight - 2, INKPLATE2_WHITE);
        
        // Draw black text over the unfilled portion
        display.setTextColor(INKPLATE2_BLACK);
        display.setCursor(textX, textY);
        display.print(percentText);
    }
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

// Helper functions for better code organization
void initializeDisplay() {
    display.begin();
    display.clearDisplay();
}

bool ensureDatesConfigured() {
    loadConfig();

    // Force reconfiguration if requested
    if (forceReconfigure) {
        Serial.println("Reconfiguration mode activated");
        configureWiFiAndDates();
        forceReconfigure = false; // Reset flag
    }

    while (!areDatesValid()) {
        Serial.println("Dates not configured, entering setup mode");
        configureWiFiAndDates();
        if (!areDatesValid()) {
            Serial.println("Dates still not configured. Please connect to Pregometer-Setup WiFi and enter dates.");
            delay(5000);
        }
    }
    return true;
}

void parseDateStrings() {
    parseDateString(start_date, &startDate);
    parseDateString(due_date, &dueDate);
    Serial.println("Dates configured successfully!");
}

bool isFirstRun() {
    return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
}

void syncTime() {
    ensureWiFiConnected();
    network.setTime(TIMEZONE_OFFSET);
    network.getTime(&currentTime);
}

void setupRTC() {
    rtc.setTimezone(TIMEZONE_OFFSET);
}

struct tm calculateNextAlarmTime() {
    struct tm alarmTime = {0};
    alarmTime.tm_hour = WAKE_HOUR;
    alarmTime.tm_min = WAKE_MINUTE;
    alarmTime.tm_sec = 0;
    
    if (currentTime.tm_hour > WAKE_HOUR || 
        (currentTime.tm_hour == WAKE_HOUR && currentTime.tm_min >= WAKE_MINUTE)) {
        alarmTime.tm_mday = currentTime.tm_mday + 1;
        alarmTime.tm_mon = currentTime.tm_mon;
    } else {
        alarmTime.tm_mday = currentTime.tm_mday;
        alarmTime.tm_mon = currentTime.tm_mon;
    }
    
    return alarmTime;
}

void showErrorMessage(const char* message) {
    display.setTextColor(INKPLATE2_BLACK);
    display.setCursor(10, 20);
    display.setTextSize(1);
    display.println(message);
    display.display();
}

void displayDaysRemaining(int daysRemaining) {
    // Format the number
    char daysText[10];
    sprintf(daysText, "%d", daysRemaining);

    // Measure the number's dimensions
    display.setFont(&FreeSansBold24pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(daysText, 0, 0, &x1, &y1, &w, &h);

    // Center the number above "days left" text (which is at x=15)
    // "days left" width is approximately 60-70 pixels with FreeSans9pt7b
    // Center the number in approximately the same horizontal space
    int centerX = 45; // Center point for left column
    int numberX = centerX - (w / 2);

    display.setCursor(numberX, 39);
    display.print(daysText);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(15, 54);
    display.println("days left");
}

void displayWeekInfo(int currentWeek, int currentTrimester) {
    display.setFont(&FreeSans12pt7b);
    display.setCursor(100, 25);
    display.printf("Week %d", currentWeek);
    
    display.setFont(&FreeSans9pt7b);
    display.setCursor(100, 40);
    display.printf("Trimester %d", currentTrimester);
}

void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, attempting to reconnect...");
        WiFi.begin();
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi reconnected");
        } else {
            Serial.println("\nFailed to reconnect to WiFi");
        }
    }
}