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
char birth_date[11] = "";
char baby_name[32] = "";
bool shouldSaveConfig = false;
bool forceReconfigure = false;

// Date structures
struct tm dueDate = {0};
struct tm startDate = {0};
struct tm birthDate = {0};
struct tm currentTime;

// Zodiac sign determination
const char* getZodiacSign(int month, int day) {
    if ((month == 3 && day >= 21) || (month == 4 && day <= 19)) return "Aries";
    if ((month == 4 && day >= 20) || (month == 5 && day <= 20)) return "Taurus";
    if ((month == 5 && day >= 21) || (month == 6 && day <= 20)) return "Gemini";
    if ((month == 6 && day >= 21) || (month == 7 && day <= 22)) return "Cancer";
    if ((month == 7 && day >= 23) || (month == 8 && day <= 22)) return "Leo";
    if ((month == 8 && day >= 23) || (month == 9 && day <= 22)) return "Virgo";
    if ((month == 9 && day >= 23) || (month == 10 && day <= 22)) return "Libra";
    if ((month == 10 && day >= 23) || (month == 11 && day <= 21)) return "Scorpio";
    if ((month == 11 && day >= 22) || (month == 12 && day <= 21)) return "Sagittarius";
    if ((month == 12 && day >= 22) || (month == 1 && day <= 19)) return "Capricorn";
    if ((month == 1 && day >= 20) || (month == 2 && day <= 18)) return "Aquarius";
    if ((month == 2 && day >= 19) || (month == 3 && day <= 20)) return "Pisces";
    return "Unknown";
}

// Get zodiac symbol (simplified text representation)
const char* getZodiacSymbol(const char* sign) {
    if (strcmp(sign, "Aries") == 0) return "Y";      // Ram horns
    if (strcmp(sign, "Taurus") == 0) return "O";     // Bull head
    if (strcmp(sign, "Gemini") == 0) return "II";    // Twins
    if (strcmp(sign, "Cancer") == 0) return "69";    // Crab
    if (strcmp(sign, "Leo") == 0) return "Q";        // Lion
    if (strcmp(sign, "Virgo") == 0) return "M";      // Virgin
    if (strcmp(sign, "Libra") == 0) return "=";      // Scales
    if (strcmp(sign, "Scorpio") == 0) return "M";    // Scorpion
    if (strcmp(sign, "Sagittarius") == 0) return "}"; // Arrow
    if (strcmp(sign, "Capricorn") == 0) return "V";  // Goat
    if (strcmp(sign, "Aquarius") == 0) return "~";   // Water
    if (strcmp(sign, "Pisces") == 0) return "X";     // Fish
    return "?";
}

// Get all 12 zodiac signs in order starting from a specific sign
void getZodiacOrder(const char* startSign, const char* signs[12]) {
    const char* allSigns[12] = {"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
                                 "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"};

    int startIdx = 0;
    for (int i = 0; i < 12; i++) {
        if (strcmp(allSigns[i], startSign) == 0) {
            startIdx = i;
            break;
        }
    }

    for (int i = 0; i < 12; i++) {
        signs[i] = allSigns[(startIdx + i) % 12];
    }
}

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
                    if (json.containsKey("birth_date")) {
                        strcpy(birth_date, json["birth_date"]);
                    }
                    if (json.containsKey("baby_name")) {
                        strcpy(baby_name, json["baby_name"]);
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
    time_t now = mktime(&currentTime);
    time_t due = mktime(&dueDate);
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
    Serial.println("Send 'RECONFIGURE' within 30 seconds to change dates...");
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
    WiFiManagerParameter custom_birth_date("birth_date", "Birth Date (YYYY-MM-DD, optional)", birth_date, 11);
    WiFiManagerParameter custom_baby_name("baby_name", "Baby Name (optional)", baby_name, 32);

    // Add parameters to WiFiManager
    wifiManager.addParameter(&custom_start_date);
    wifiManager.addParameter(&custom_due_date);
    wifiManager.addParameter(&custom_birth_date);
    wifiManager.addParameter(&custom_baby_name);
    
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
    strcpy(birth_date, custom_birth_date.getValue());
    strcpy(baby_name, custom_baby_name.getValue());

    // Log the configured dates
    Serial.print("Start date: ");
    Serial.println(start_date);
    Serial.print("Due date: ");
    Serial.println(due_date);
    Serial.print("Birth date: ");
    Serial.println(birth_date);
    Serial.print("Baby name: ");
    Serial.println(baby_name);
    
    // Save configuration if changed
    if (shouldSaveConfig) {
        saveConfig();
    }
}

void setup() {
    Serial.begin(115200);
    delay(100); // Give serial time to initialize
    initializeDisplay();

    // Only check for reconfigure command on manual reset/power-on (not timer wake-up)
    // Timer wake-ups are unattended battery operation - no point waiting for serial
    if (isFirstRun()) {
        forceReconfigure = checkForReconfigureCommand();
    }

    if (!ensureDatesConfigured()) {
        Serial.println("Failed to configure dates");
        return;
    }

    parseDateStrings();

    if (isFirstRun()) {
        setupDailyAlarm();
    }

    updateDisplay();
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
        if (isPostBirthMode()) {
            showBirthProgress();
        } else {
            showPregnancyProgress();
        }
        deepSleep();
    } else {
        showErrorMessage("Could not set alarm");
        deepSleep();
    }
}

void updateDisplay() {
    syncTime();
    if (isPostBirthMode()) {
        showBirthProgress();
    } else {
        showPregnancyProgress();
    }
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

// Birth mode age calculation functions
int calculateDaysSinceBirth() {
    struct tm effectiveBirth = getEffectiveBirthDate();
    time_t now = mktime(&currentTime);
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
    time_t birth = mktime(&effectiveBirth);
    time_t due = mktime(&dueDate);

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
    time_t birth = mktime(&effectiveBirth);
    time_t due = mktime(&dueDate);

    return difftime(due, birth) > 0;
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
    if (isBirthDateValid()) {
        parseDateString(birth_date, &birthDate);
    }
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

// Birth mode display functions
void showBirthProgress() {
    int daysSinceBirth = calculateDaysSinceBirth();
    int weeks = calculateWeeksSinceBirth();
    int months = calculateMonthsSinceBirth();

    display.clearDisplay();
    display.setTextColor(INKPLATE2_BLACK);

    // Display baby name at top if configured
    if (strlen(baby_name) > 0) {
        display.setFont(&FreeSans9pt7b);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(baby_name, 0, 0, &x1, &y1, &w, &h);
        int nameX = (212 - w) / 2;  // Center horizontally
        display.setCursor(nameX, 12);
        display.print(baby_name);
    }

    // Get birth sign for zodiac display
    struct tm effectiveBirth = getEffectiveBirthDate();
    const char* birthSign = getZodiacSign(effectiveBirth.tm_mon + 1, effectiveBirth.tm_mday);

    // Calculate percentage of first year complete (0-100%)
    float yearProgress = (daysSinceBirth / 365.0) * 100.0;
    if (yearProgress > 100.0) yearProgress = 100.0;

    // Draw circular progress indicator on left side
    int circleY = strlen(baby_name) > 0 ? 60 : 52;  // Adjust position if name is shown
    drawCircularProgress(45, circleY, 25, yearProgress, birthSign);

    // Display age info on right side (compact format)
    displayCompactAgeInfo(daysSinceBirth, weeks, months);

    // Show corrected age if baby was born early
    if (wasBornEarly()) {
        displayCorrectedAge();
    }

    display.display();
}

// Compact age display for new layout
void displayCompactAgeInfo(int days, int weeks, int months) {
    int startX = 100;
    int startY = strlen(baby_name) > 0 ? 35 : 30;

    // Display days in larger font
    display.setFont(&FreeSans18pt7b);
    display.setCursor(startX, startY);
    display.printf("%d", days);

    // Display "days" label
    display.setFont(&FreeSans9pt7b);
    display.setCursor(startX, startY + 18);
    display.print("days");

    // Display weeks and months
    display.setCursor(startX, startY + 36);
    display.printf("%dw", weeks);

    display.setCursor(startX, startY + 54);
    display.printf("%dm", months);
}

void displayDaysOld(int daysOld) {
    char daysText[10];
    sprintf(daysText, "%d", daysOld);

    display.setFont(&FreeSansBold24pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(daysText, 0, 0, &x1, &y1, &w, &h);

    int centerX = 45;
    int numberX = centerX - (w / 2);

    display.setCursor(numberX, 39);
    display.print(daysText);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(15, 54);
    display.println("days old");
}

void displayAgeInfo(int weeks, int months) {
    display.setFont(&FreeSans12pt7b);
    display.setCursor(100, 25);
    display.printf("%d weeks", weeks);

    display.setFont(&FreeSans12pt7b);
    display.setCursor(100, 50);
    if (months == 1) {
        display.printf("%d month", months);
    } else {
        display.printf("%d months", months);
    }
}

void displayCorrectedAge() {
    int correctedDays = calculateCorrectedAgeDays();
    int correctedWeeks = correctedDays / 7;
    int correctedMonths = correctedDays / 30;  // Approximate

    display.setFont(&FreeSans9pt7b);
    display.setCursor(5, 95);
    display.printf("Corrected: %dw / %dm", correctedWeeks, correctedMonths);
}

// Draw circular progress indicator with zodiac symbols
void drawCircularProgress(int centerX, int centerY, int radius, float percentComplete, const char* birthSign) {
    // Draw outer circle
    display.drawCircle(centerX, centerY, radius, INKPLATE2_BLACK);
    display.drawCircle(centerX, centerY, radius - 1, INKPLATE2_BLACK);

    // Calculate progress arc (0-360 degrees, starting from top)
    // Convert percentage to angle (0% = 0 degrees, 100% = 360 degrees)
    float endAngle = (percentComplete / 100.0) * 360.0;

    // Draw progress arc by filling segments
    for (float angle = 0; angle < endAngle; angle += 2.0) {
        float radians = (angle - 90) * PI / 180.0;  // -90 to start at top (12 o'clock)
        int x1 = centerX + (radius - 6) * cos(radians);
        int y1 = centerY + (radius - 6) * sin(radians);
        int x2 = centerX + (radius - 2) * cos(radians);
        int y2 = centerY + (radius - 2) * sin(radians);
        display.drawLine(x1, y1, x2, y2, INKPLATE2_BLACK);
    }

    // Draw zodiac symbols around the circle
    const char* zodiacOrder[12];
    getZodiacOrder(birthSign, zodiacOrder);

    display.setFont(&FreeSans9pt7b);
    for (int i = 0; i < 12; i++) {
        // Calculate angle for each zodiac (30 degrees apart, starting from top)
        float angle = i * 30.0;
        float radians = (angle - 90) * PI / 180.0;  // -90 to start at top

        const char* symbol = getZodiacSymbol(zodiacOrder[i]);

        // Position symbols outside the circle
        int symbolX = centerX + (radius + 10) * cos(radians);
        int symbolY = centerY + (radius + 10) * sin(radians);

        // Adjust text position to center the symbol
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(symbol, 0, 0, &x1, &y1, &w, &h);

        display.setCursor(symbolX - w/2, symbolY + h/2);
        display.setTextColor(INKPLATE2_RED);  // Use red for zodiac symbols
        display.print(symbol);
    }
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
