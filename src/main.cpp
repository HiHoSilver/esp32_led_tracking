#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "auth.h"

// ESP32 constants
const int ledPin = 2;
const int buttonOn = 4;
const int buttonOff = 5;

unsigned long debounceDelay = 50;
unsigned long lastOnPress = 0;
unsigned long lastOffPress = 0;

bool lastOnState  = HIGH;
bool lastOffState = HIGH;

// Home server endpoint
const char* serverUrl = "http://192.168.1.171:5000/api/esp32_led";

// Local ESP32 server
WebServer server(80);

// ---------------- WiFi ----------------

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void ensureWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
}

// ---------------- Sync from server ----------------

void syncStateFromServer() {
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(serverUrl);
    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        Serial.println("Sync response: " + payload);

        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.print("JSON parse error: ");
            Serial.println(err.c_str());
            return;
        }

        // Expecting: { "state": true } or { "state": false }
        bool state = doc["state"] | false;

        digitalWrite(ledPin, state ? HIGH : LOW);
    }

    http.end();
}

// ---------------- Send state to server ----------------

void sendStateToServer(const char* state) {
    HTTPClient http;
    http.setTimeout(1500);
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<128> doc;
    doc["state"] = state;  // "on" or "off"

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);

    Serial.print("POST ");
    Serial.print(state);
    Serial.print(" -> ");
    Serial.println(code);

    http.end();
}

// ---------------- Receive broadcast from server ----------------

void handleLedUpdate() {
    String body = server.arg("plain");
    Serial.println("Received POST: " + body);

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    const char* state = doc["state"];

    if (strcmp(state, "on") == 0) {
        digitalWrite(ledPin, HIGH);
        Serial.println("LED set to ON");
    } 
    else if (strcmp(state, "off") == 0) {
        digitalWrite(ledPin, LOW);
        Serial.println("LED set to OFF");
    } 
    else {
        Serial.println("Invalid state received");
    }

    server.send(200, "application/json", "{\"success\":true}");
}

// ---------------- Setup ----------------

void setup() {
    Serial.begin(9600);
    pinMode(ledPin, OUTPUT);
    pinMode(buttonOn, INPUT_PULLUP);
    pinMode(buttonOff, INPUT_PULLUP);

    connectWiFi();

    server.on("/led", HTTP_POST, handleLedUpdate);
    server.begin();

    Serial.println("Setup complete.");

    syncStateFromServer();
    Serial.println("Server synchronization complete.");
}

// ---------------- Loop ----------------

void loop() {
    ensureWiFi();
    server.handleClient();

    unsigned long now = millis();

    // Read current button states
    bool onState  = digitalRead(buttonOn);
    bool offState = digitalRead(buttonOff);

    // ON button: detect HIGH → LOW transition
    if (lastOnState == HIGH && onState == LOW && (now - lastOnPress > debounceDelay)) {
        digitalWrite(ledPin, HIGH);
        sendStateToServer("on");
        lastOnPress = now;
        Serial.println("On button pressed...");
    }

    // OFF button: detect HIGH → LOW transition
    if (lastOffState == HIGH && offState == LOW && (now - lastOffPress > debounceDelay)) {
        digitalWrite(ledPin, LOW);
        sendStateToServer("off");
        lastOffPress = now;
        Serial.println("Off button pressed...");
    }

    // Save states for next loop
    lastOnState  = onState;
    lastOffState = offState;
}
