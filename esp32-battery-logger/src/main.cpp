#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA260.h>
#include <U8g2lib.h>
#include <multi_channel_relay.h>
#include "BatteryLog.h"

// INA260 on I2C (default XIAO ESP32S3 pins via Wire.begin(): SDA=GPIO5, SCL=GPIO6)
Adafruit_INA260 ina260;
BatteryLog batteryLog;

// Seeed Grove 4-channel relay module (STM8S), default I2C address 0x11.
Multi_Channel_Relay relay;
static constexpr uint8_t RELAY_I2C_ADDR = 0x11;
static constexpr int     RELAY_CH_HEATER = 3;

// Seeed Grove 1.12" OLED (SH1107), default 7-bit I2C address.
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static constexpr uint8_t OLED_I2C_ADDR = 0x3C;

// Set once at startup after probing the I2C bus, so the app runs headless
// (logging still works) if no OLED is wired up — a floating/pulled-down SDA
// line with no device present can otherwise hang the Wire bus indefinitely.
static bool displayAvailable = false;

static bool probeOledDisplay() {
    Wire.beginTransmission(OLED_I2C_ADDR);
    return Wire.endTransmission() == 0;
}

static constexpr int LED_PIN = D0;

static constexpr uint32_t COMMAND_WINDOW_MS  = 15000;
static constexpr uint32_t MEASURE_INTERVAL_MS = 15000;

enum AppMode { MODE_INIT, MODE_COMMAND, MODE_LOGGING };
static AppMode appMode = MODE_INIT;

void blinkLed(int count, int onMs, int offMs) {
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(onMs);
        digitalWrite(LED_PIN, LOW);
        if (i < count - 1) delay(offMs);
    }
}

void updateDisplay(float voltage, float currentA) {
    if (!displayAvailable) return;

    char timeBuf[10];
    batteryLog.formatElapsed(timeBuf, sizeof(timeBuf));

    char line[24];
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0, 20, "Battery Logger");

    u8g2.setFont(u8g2_font_6x12_tr);
    snprintf(line, sizeof(line), "Runtime: %s", timeBuf);
    u8g2.drawStr(0, 44, line);

    snprintf(line, sizeof(line), "V: %.2f V", voltage);
    u8g2.drawStr(0, 62, line);

    snprintf(line, sizeof(line), "I: %.3f A", currentA);
    u8g2.drawStr(0, 80, line);

    u8g2.sendBuffer();
}

void handleSerialCommand() {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) return;

    if (line == "list") {
        batteryLog.listFiles();
    } else if (line == "delete") {
        batteryLog.deleteAllFiles();
    } else if (line.startsWith("show ")) {
        batteryLog.showFile(line.substring(5));
    } else {
        Serial.print("[Cmd] Unknown command: ");
        Serial.println(line);
        Serial.println("[Cmd] Available: list, delete, show NAME");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    delay(3000);

    Serial.println("=== Battery Voltage Logger ===");

    Wire.begin();

    Serial.print("[INA260] Initializing ... ");
    if (ina260.begin()) {
        Serial.println("OK");
    } else {
        Serial.println("FAILED - check wiring");
        while (true) { blinkLed(1, 100, 0); delay(900); }
    }

    if (!batteryLog.mount()) {
        Serial.println("[Log] Could not initialize storage - halting");
        while (true) { blinkLed(2, 100, 100); delay(900); }
    }

    Serial.print("[OLED] Probing I2C display ... ");
    displayAvailable = probeOledDisplay();
    if (displayAvailable) {
        Serial.println("found");
        u8g2.begin();
    } else {
        Serial.println("not found - running headless (serial only)");
    }

    relay.begin(RELAY_I2C_ADDR);

    Serial.println();
    Serial.print("Serial command window (");
    Serial.print(COMMAND_WINDOW_MS / 1000);
    Serial.println("s) - commands: list, delete, show NAME");

    uint32_t cmdWindowEnd = millis() + COMMAND_WINDOW_MS;
    while (millis() < cmdWindowEnd) {
        if (Serial.available()) {
            appMode = MODE_COMMAND;
            Serial.println("Entered command mode (voltage logging disabled)");
            handleSerialCommand();
            break;
        }
        delay(50);
    }

    if (appMode != MODE_COMMAND) {
        appMode = MODE_LOGGING;
        batteryLog.startNewFile();
        relay.turn_on_channel(RELAY_CH_HEATER);
        Serial.println("No commands - starting voltage logging, heater relay ON");
        blinkLed(3, 150, 150);
    }
}

void loop() {
    if (appMode == MODE_COMMAND) {
        if (Serial.available()) {
            handleSerialCommand();
        }
        delay(50);
        return;
    }

    static uint32_t nextMeasure = 0;
    if (millis() >= nextMeasure) {
        nextMeasure = millis() + MEASURE_INTERVAL_MS;
        float voltage = ina260.readBusVoltage() / 1000.0f;
        float current = ina260.readCurrent() / 1000.0f;
        batteryLog.record(voltage);
        updateDisplay(voltage, current);
        blinkLed(1, 50, 0);
    }
    delay(50);
}
