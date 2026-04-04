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

// Debug boot log - shows progress on eInk display
// Set to false to disable debug overlay
const bool DEBUG_BOOT = false;
int debugLine = 0;

void debugStatus(const char* msg) {
    if (!DEBUG_BOOT) return;
    Serial.println(msg);
    display.setFont(NULL); // built-in 5x7 font
    display.setTextSize(1);
    display.setTextColor(INKPLATE2_BLACK);
    display.setCursor(2, 2 + debugLine * 10);
    display.print(msg);
    display.display();
    debugLine++;
}

// Configuration variables
char start_date[11] = "";
char due_date[11] = "";
char birth_date[11] = "";
char baby_name[32] = "";
bool shouldSaveConfig = false;
bool forceReconfigure = false;

// Date structures
struct tm dueDate = {0};
struct tm startDate = {0};
struct tm birthDate = {0};
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
                    strncpy(start_date, json["start_date"] | "", sizeof(start_date) - 1);
                    start_date[sizeof(start_date) - 1] = '\0';
                    strncpy(due_date, json["due_date"] | "", sizeof(due_date) - 1);
                    due_date[sizeof(due_date) - 1] = '\0';
                    if (json.containsKey("birth_date")) {
                        strncpy(birth_date, json["birth_date"] | "", sizeof(birth_date) - 1);
                        birth_date[sizeof(birth_date) - 1] = '\0';
                    }
                    if (json.containsKey("baby_name")) {
                        strncpy(baby_name, json["baby_name"] | "", sizeof(baby_name) - 1);
                        baby_name[sizeof(baby_name) - 1] = '\0';
                    }
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
    json["birth_date"] = birth_date;
    json["baby_name"] = baby_name;

    fs::File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
    } else {
        serializeJson(json, configFile);
        configFile.close();
        Serial.println("config saved");
    }
}

// Boot stats - persisted to SPIFFS
struct BootStats {
    int boots;
    int wifi_ok;
    int wifi_fail;
    int ntp_ok;
    int ntp_fail;
    int alarm_ok;
    int alarm_fail;
    char last_boot[20];   // YYYY-MM-DD HH:MM:SS
    int last_wifi_rssi;
};

BootStats stats = {0};

void loadStats() {
    if (!SPIFFS.exists("/stats.json")) return;
    fs::File f = SPIFFS.open("/stats.json", "r");
    if (!f) return;
    DynamicJsonDocument json(512);
    if (deserializeJson(json, f)) { f.close(); return; }
    f.close();
    stats.boots = json["boots"] | 0;
    stats.wifi_ok = json["wifi_ok"] | 0;
    stats.wifi_fail = json["wifi_fail"] | 0;
    stats.ntp_ok = json["ntp_ok"] | 0;
    stats.ntp_fail = json["ntp_fail"] | 0;
    stats.alarm_ok = json["alarm_ok"] | 0;
    stats.alarm_fail = json["alarm_fail"] | 0;
    stats.last_wifi_rssi = json["rssi"] | 0;
    strncpy(stats.last_boot, json["last_boot"] | "", sizeof(stats.last_boot) - 1);
}

void saveStats() {
    DynamicJsonDocument json(512);
    json["boots"] = stats.boots;
    json["wifi_ok"] = stats.wifi_ok;
    json["wifi_fail"] = stats.wifi_fail;
    json["ntp_ok"] = stats.ntp_ok;
    json["ntp_fail"] = stats.ntp_fail;
    json["alarm_ok"] = stats.alarm_ok;
    json["alarm_fail"] = stats.alarm_fail;
    json["rssi"] = stats.last_wifi_rssi;
    json["last_boot"] = stats.last_boot;
    fs::File f = SPIFFS.open("/stats.json", "w");
    if (f) { serializeJson(json, f); f.close(); }
}

void printStats() {
    Serial.println("\n=== Boot Stats ===");
    Serial.printf("  boots:     %d\n", stats.boots);
    Serial.printf("  wifi:      %d ok / %d fail\n", stats.wifi_ok, stats.wifi_fail);
    Serial.printf("  ntp:       %d ok / %d fail\n", stats.ntp_ok, stats.ntp_fail);
    Serial.printf("  alarm:     %d ok / %d fail\n", stats.alarm_ok, stats.alarm_fail);
    Serial.printf("  last rssi: %d dBm\n", stats.last_wifi_rssi);
    Serial.printf("  last boot: %s\n", stats.last_boot);
    Serial.println("==================\n");
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

bool isBirthDateValid() {
    return (strlen(birth_date) == 10 &&
            birth_date[4] == '-' && birth_date[7] == '-');
}

bool isPostBirthMode() {
    // If birth date is configured, use birth mode
    if (isBirthDateValid()) {
        return true;
    }
    // Auto-switch if current date is past due date
    struct tm ct = currentTime;
    struct tm dd = dueDate;
    time_t now = mktime(&ct);
    time_t due = mktime(&dd);
    return difftime(now, due) > 0;
}

struct tm getEffectiveBirthDate() {
    // If birth date is configured, use it; otherwise use due date
    if (isBirthDateValid()) {
        return birthDate;
    }
    return dueDate;
}

bool checkForReconfigureCommand() {
    Serial.println("Send 'RECONFIGURE' or 'STATS' within 30 seconds...");
    unsigned long startTime = millis();
    String input = "";

    while (millis() - startTime < 30000) {
        if (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.equalsIgnoreCase("RECONFIGURE")) {
                    Serial.println("Reconfiguration requested!");
                    return true;
                }
                if (input.equalsIgnoreCase("STATS")) {
                    printStats();
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

// Sets up WiFiManager with date parameters, calls connectFunc to
// either force the portal or try saved creds first, then saves config.
bool runWiFiManager(bool (*connectFunc)(WiFiManager&)) {
    WiFiManager wifiManager;
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setConfigPortalTimeout(300); // 5 minutes

    WiFiManagerParameter custom_start_date("start_date", "Start Date (YYYY-MM-DD)", start_date, 11);
    WiFiManagerParameter custom_due_date("due_date", "Due Date (YYYY-MM-DD)", due_date, 11);
    WiFiManagerParameter custom_birth_date("birth_date", "Birth Date (YYYY-MM-DD, optional)", birth_date, 11);
    WiFiManagerParameter custom_baby_name("baby_name", "Baby's Name", baby_name, 32);

    wifiManager.addParameter(&custom_start_date);
    wifiManager.addParameter(&custom_due_date);
    wifiManager.addParameter(&custom_birth_date);
    wifiManager.addParameter(&custom_baby_name);

    if (!connectFunc(wifiManager)) {
        Serial.println("WiFiManager: failed to connect");
        return false;
    }

    Serial.println("WiFi connected");

    strcpy(start_date, custom_start_date.getValue());
    strcpy(due_date, custom_due_date.getValue());
    strcpy(birth_date, custom_birth_date.getValue());
    strcpy(baby_name, custom_baby_name.getValue());

    Serial.print("Start date: ");
    Serial.println(start_date);
    Serial.print("Due date: ");
    Serial.println(due_date);
    Serial.print("Birth date: ");
    Serial.println(birth_date);
    Serial.print("Baby name: ");
    Serial.println(baby_name);

    if (shouldSaveConfig) {
        saveConfig();
    }
    return true;
}

bool wifiForcePortal(WiFiManager& wm) {
    Serial.println("Opening config portal (Pregometer-Setup)...");
    wm.resetSettings();
    return wm.startConfigPortal("Pregometer-Setup");
}

bool wifiAutoConnect(WiFiManager& wm) {
    Serial.println("Trying saved WiFi, falling back to config portal...");
    return wm.autoConnect("Pregometer-Setup");
}

void configureWiFiAndDates() {
    runWiFiManager(wifiForcePortal);
}

void setup() {
    Serial.begin(115200);
    delay(100); // Give serial time to initialize
    initializeDisplay();

    debugStatus(isFirstRun() ? "BOOT: power on" : "BOOT: timer wakeup");

    // Only check for reconfigure command on manual reset/power-on (not timer wake-up)
    // Timer wake-ups are unattended battery operation - no point waiting for serial
    if (isFirstRun()) {
        forceReconfigure = checkForReconfigureCommand();
    }

    debugStatus("CONFIG: loading...");
    if (!ensureDatesConfigured()) {
        debugStatus("CONFIG: FAILED");
        return;
    }

    loadStats();
    stats.boots++;
    debugStatus("CONFIG: ok");

    parseDateStrings();
    setupRTC();

    updateDisplay();
}

void loop() {
    // Empty - everything happens in setup() due to deep sleep
}

void updateDisplay() {
    debugStatus("WIFI: connecting...");
    if (!syncTime()) {
        debugStatus("WIFI/NTP: FAILED");
        showWiFiError();
        deepSleep();
        return;
    }
    debugStatus("NTP: ok");
    debugLine = 0; // reset for next boot
    if (isPostBirthMode()) {
        showBirthProgress();
    } else {
        showPregnancyProgress();
    }
    setNextDailyAlarm();
    deepSleep();
}

void showWiFiError() {
    display.clearDisplay();
    display.setTextColor(INKPLATE2_BLACK);

    String failedSSID = WiFi.SSID();

    display.setFont(&FreeSans12pt7b);
    display.setCursor(20, 30);
    display.print("No WiFi");

    display.setFont(&FreeSans9pt7b);
    if (failedSSID.length() > 0) {
        display.setCursor(20, 50);
        display.printf("Can't reach %s", failedSSID.c_str());
    }
    display.setCursor(20, 72);
    display.print("Connect to");
    display.setCursor(20, 89);
    display.print("Pregometer-Setup");

    display.display();

    // Try saved creds first, fall back to config portal for 5 minutes
    if (runWiFiManager(wifiAutoConnect)) {
        ESP.restart();
    }

    // Portal timed out — retry in 1 hour
    Serial.println("Config portal timed out, retrying in 1 hour");
    esp_sleep_enable_timer_wakeup(3600ULL * 1000000ULL);
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
    struct tm ct = currentTime;
    struct tm dd = dueDate;
    time_t now = mktime(&ct);
    time_t due = mktime(&dd);
    
    double diffSeconds = difftime(due, now);
    int diffDays = (int)(diffSeconds / (24 * 3600));
    
    return diffDays > 0 ? diffDays : 0;
}

int calculateCurrentWeek() {
    struct tm ct = currentTime;
    struct tm sd = startDate;
    time_t now = mktime(&ct);
    time_t start = mktime(&sd);
    
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
    struct tm ct = currentTime;
    struct tm sd = startDate;
    time_t now = mktime(&ct);
    time_t start = mktime(&sd);

    double diffSeconds = difftime(now, start);
    int daysPassed = (int)(diffSeconds / (24 * 3600));

    float percent = ((float)daysPassed / TOTAL_PREGNANCY_DAYS) * 100.0;
    return percent > 100.0 ? 100.0 : (percent < 0.0 ? 0.0 : percent);
}

// Birth mode age calculation functions
int calculateDaysSinceBirth() {
    struct tm effectiveBirth = getEffectiveBirthDate();
    struct tm ct = currentTime;
    time_t now = mktime(&ct);
    time_t birth = mktime(&effectiveBirth);

    double diffSeconds = difftime(now, birth);
    int diffDays = (int)(diffSeconds / (24 * 3600));

    return diffDays > 0 ? diffDays : 0;
}

int calculateWeeksSinceBirth() {
    return calculateDaysSinceBirth() / 7;
}

int calculateMonthsSinceBirth() {
    struct tm effectiveBirth = getEffectiveBirthDate();

    int months = (currentTime.tm_year - effectiveBirth.tm_year) * 12;
    months += (currentTime.tm_mon - effectiveBirth.tm_mon);

    // Adjust if we haven't reached the day of the month yet
    if (currentTime.tm_mday < effectiveBirth.tm_mday) {
        months--;
    }

    return months > 0 ? months : 0;
}

// Corrected age for premature babies
int calculateCorrectedAgeDays() {
    // Corrected age = chronological age - (due date - birth date)
    struct tm effectiveBirth = getEffectiveBirthDate();
    struct tm dd = dueDate;
    time_t birth = mktime(&effectiveBirth);
    time_t due = mktime(&dd);

    // Days early = due date - birth date
    double earlySeconds = difftime(due, birth);
    int daysEarly = (int)(earlySeconds / (24 * 3600));

    if (daysEarly <= 0) {
        // Baby was born on or after due date, no correction needed
        return calculateDaysSinceBirth();
    }

    int correctedDays = calculateDaysSinceBirth() - daysEarly;
    return correctedDays > 0 ? correctedDays : 0;
}

bool wasBornEarly() {
    struct tm effectiveBirth = getEffectiveBirthDate();
    struct tm dd = dueDate;
    time_t birth = mktime(&effectiveBirth);
    time_t due = mktime(&dd);

    return difftime(due, birth) > 0;
}


void setNextDailyAlarm() {
    struct tm alarmTime = {0};
    alarmTime.tm_hour = WAKE_HOUR;
    alarmTime.tm_min = WAKE_MINUTE;
    alarmTime.tm_sec = 0;
    alarmTime.tm_mday = currentTime.tm_mday + 1;
    alarmTime.tm_mon = currentTime.tm_mon;

    double result = rtc.setAlarm(alarmTime, RTC_DHHMMSS);
    if (result > 0) {
        stats.alarm_ok++;
    } else {
        stats.alarm_fail++;
    }
    saveStats();
}

void deepSleep() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
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
    if (isBirthDateValid()) {
        parseDateString(birth_date, &birthDate);
    }
    Serial.println("Dates configured successfully!");
}

bool isFirstRun() {
    return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
}

bool syncTime() {
    bool wifiOk = ensureWiFiConnected();
    if (wifiOk) {
        stats.wifi_ok++;
        stats.last_wifi_rssi = WiFi.RSSI();
        if (network.setTime(TIMEZONE_OFFSET)) {
            stats.ntp_ok++;
            network.getTime(&currentTime);
            snprintf(stats.last_boot, sizeof(stats.last_boot),
                "%04d-%02d-%02d %02d:%02d:%02d",
                currentTime.tm_year+1900, currentTime.tm_mon+1, currentTime.tm_mday,
                currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);
            saveStats();
            return true;
        }
        stats.ntp_fail++;
    } else {
        stats.wifi_fail++;
    }
    saveStats();

    // WiFi or NTP failed — on timer wake-ups the ESP32 RTC still has a roughly
    // correct time from the last NTP sync, so just use it.
    if (!isFirstRun()) {
        Serial.println("WiFi/NTP failed, using ESP32 RTC time");
        network.getTime(&currentTime);
        return true;
    }

    // First boot with no prior NTP sync — time is meaningless
    return false;
}

void setupRTC() {
    rtc.setTimezone(TIMEZONE_OFFSET);
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

// Helper function to draw a filled arc (pie slice)
void fillArc(int centerX, int centerY, int radius, float startAngle, float endAngle, int color) {
    // Draw filled arc using small triangles
    float angleStep = 0.05;  // Small steps for smooth arc
    for (float angle = startAngle; angle < endAngle; angle += angleStep) {
        float nextAngle = angle + angleStep;
        if (nextAngle > endAngle) nextAngle = endAngle;

        int x1 = centerX + radius * cos(angle);
        int y1 = centerY + radius * sin(angle);
        int x2 = centerX + radius * cos(nextAngle);
        int y2 = centerY + radius * sin(nextAngle);

        display.fillTriangle(centerX, centerY, x1, y1, x2, y2, color);
    }
}

void displayFirstYearProgress(int daysSinceBirth) {
    const int DAYS_IN_YEAR = 365;
    float percent = (float)daysSinceBirth / DAYS_IN_YEAR * 100.0;
    if (percent > 100.0) percent = 100.0;

    // Circle parameters - positioned on right side
    int centerX = 168;
    int centerY = 60;
    int radius = 36;
    int innerRadius = 24;

    // Draw background circle outline
    display.drawCircle(centerX, centerY, radius, INKPLATE2_BLACK);

    // Draw filled arc (clock-style, starting from 12 o'clock)
    if (percent > 0) {
        float startAngle = -M_PI / 2;  // 12 o'clock position
        float endAngle = startAngle + (M_PI * 2 * percent / 100.0);
        fillArc(centerX, centerY, radius, startAngle, endAngle, INKPLATE2_BLACK);
    }

    // Draw inner white circle to create donut effect
    display.fillCircle(centerX, centerY, innerRadius, INKPLATE2_WHITE);

    // Draw inner circle outline
    display.drawCircle(centerX, centerY, innerRadius, INKPLATE2_BLACK);

    // Draw percentage text in center
    display.setFont(&FreeSans9pt7b);
    char percentText[10];
    sprintf(percentText, "%.0f%%", percent);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(percentText, 0, 0, &x1, &y1, &w, &h);

    display.setTextColor(INKPLATE2_BLACK);
    display.setCursor(centerX - w / 2, centerY + h / 2);
    display.print(percentText);
}

const char* monthAbbrev(int mon) {
    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    return months[mon];
}

void displayBabyHeader() {
    char dateStr[8];
    sprintf(dateStr, "%s %d", monthAbbrev(currentTime.tm_mon), currentTime.tm_mday);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(6, 18);
    display.printf("%s, %s is:", dateStr, baby_name);
}

// Birth mode display functions
void showBirthProgress() {
    int daysSinceBirth = calculateDaysSinceBirth();
    int weeks = calculateWeeksSinceBirth();
    int months = calculateMonthsSinceBirth();

    display.clearDisplay();
    display.setTextColor(INKPLATE2_BLACK);

    displayBabyHeader();
    displayDaysOld(daysSinceBirth);
    displayAgeInfo(weeks, months);

    // Circular progress bar for first year
    displayFirstYearProgress(daysSinceBirth);

    display.display();
}

void displayDaysOld(int daysOld) {
    char daysText[10];
    sprintf(daysText, "%d", daysOld);

    // Draw the number on the left
    display.setFont(&FreeSansBold24pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(daysText, 0, 0, &x1, &y1, &w, &h);

    int numberX = 6;
    int numberY = 60;

    display.setCursor(numberX, numberY);
    display.print(daysText);

    // Draw "days" and "old" stacked to the right of the number
    display.setFont(&FreeSans9pt7b);
    int labelX = numberX + w + 10;
    display.setCursor(labelX, numberY - 18);
    display.print("days");
    display.setCursor(labelX, numberY - 4);
    display.print("old");
}

void displayAgeInfo(int weeks, int months) {
    // Show weeks and months on separate lines below the days
    display.setFont(&FreeSans9pt7b);
    display.setCursor(6, 80);
    display.printf("%d %s", weeks, weeks == 1 ? "week" : "weeks");
    display.setCursor(6, 96);
    display.printf("%d %s", months, months == 1 ? "month" : "months");
}

bool ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

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
        return true;
    }

    Serial.println("\nFailed to reconnect to WiFi");
    return false;
}
