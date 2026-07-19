#pragma once
#include <Arduino.h>
#include <LittleFS.h>

// Logs "elapsed uptime, voltage, state" rows to /battery-N.csv on LittleFS.
// N counts up across reboots by probing for the first free filename.
class BatteryLog {
public:
    // Mounts the filesystem only — used during the command window, before
    // any new log file exists, so list/delete/show can see prior runs.
    bool mount() {
        if (!LittleFS.begin(true)) {
            Serial.println("[Log] LittleFS mount failed");
            return false;
        }
        Serial.println("[Log] LittleFS mounted");
        return true;
    }

    // Creates the next /battery-N.csv. Called once the command window has
    // passed and logging is actually starting.
    bool startNewFile() {
        findNextFileNumber();
        createLogFile();
        return !filePath.isEmpty();
    }

    // Formats time since boot (millis()) as HH:MM:SS.
    void formatElapsed(char* buf, size_t len) {
        uint32_t totalSec = millis() / 1000UL;
        uint32_t hh = totalSec / 3600UL;
        uint32_t mm = (totalSec % 3600UL) / 60UL;
        uint32_t ss = totalSec % 60UL;
        snprintf(buf, len, "%02lu:%02lu:%02lu", hh, mm, ss);
    }

    void record(float voltage, const char* state) {
        if (filePath.isEmpty()) return;

        File f = LittleFS.open(filePath, FILE_APPEND, true);
        if (!f) {
            Serial.println("[Log] Failed to open log file for append");
            return;
        }

        char timestamp[16];
        formatElapsed(timestamp, sizeof(timestamp));
        char line[48];
        snprintf(line, sizeof(line), "%s, %.2f, %s\n", timestamp, voltage, state);
        f.print(line);
        f.close();

        Serial.print("[Log] ");
        Serial.print(line);
    }

    void listFiles() {
        Serial.println("[Log] Files on LittleFS:");
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        bool any = false;
        while (f) {
            Serial.print("  ");
            Serial.print(f.name());
            Serial.print("  ");
            Serial.print(f.size());
            Serial.println(" bytes");
            any = true;
            f = root.openNextFile();
        }
        if (!any) Serial.println("  (no files)");
    }

    void deleteAllFiles() {
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        int count = 0;
        while (f) {
            String name = "/" + String(f.name());
            f = root.openNextFile();
            if (name.endsWith(".csv")) {
                LittleFS.remove(name);
                count++;
            }
        }
        Serial.print("[Log] Deleted ");
        Serial.print(count);
        Serial.println(" file(s)");
    }

    void showFile(const String& rawName) {
        String name = rawName;
        name.trim();
        if (name.isEmpty()) {
            Serial.println("[Log] Usage: show NAME");
            return;
        }
        if (!name.startsWith("/")) name = "/" + name;
        if (!name.endsWith(".csv")) name += ".csv";

        if (!LittleFS.exists(name)) {
            Serial.print("[Log] File not found: ");
            Serial.println(name);
            return;
        }

        File f = LittleFS.open(name, FILE_READ);
        Serial.print("[Log] --- ");
        Serial.print(name);
        Serial.println(" ---");
        while (f.available()) {
            Serial.write(f.read());
        }
        f.close();
        Serial.println("[Log] --- end ---");
    }

    const String& getFilePath() const { return filePath; }

private:
    void findNextFileNumber() {
        fileNumber = 1;
        while (LittleFS.exists("/battery-" + String(fileNumber) + ".csv")) {
            fileNumber++;
        }
    }

    void createLogFile() {
        filePath = "/battery-" + String(fileNumber) + ".csv";
        File f = LittleFS.open(filePath, FILE_WRITE, true);
        if (!f) {
            Serial.println("[Log] Failed to create log file");
            filePath = "";
            return;
        }
        f.println("timestamp,voltage,state");
        f.close();
        Serial.print("[Log] Created ");
        Serial.println(filePath);
    }

    int fileNumber = 1;
    String filePath;
};
