#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "auth.h"

// ESP32 constants
const int onBoardLedPin = 2;
const int greenLedPin = 18;
const int yellowLedPin = 19;
const int redLedPin = 21;

const int buttonOn = 14;
const int buttonOff = 13;

unsigned long debounceDelay = 50;
unsigned long lastOnPress = 0;
unsigned long lastOffPress = 0;

bool lastOnState  = HIGH;
bool lastOffState = HIGH;

// Home server endpoint
const char* serverUrl = "http://192.168.1.171:5000/api/esp32_led";

// Local ESP32 server
WebServer server(80);

// ---------------- LED State Management ----------------
void setStateLeds(bool state) {
    digitalWrite(greenLedPin, state ? LOW : HIGH);
    digitalWrite(redLedPin, state ? HIGH: LOW);
}

void setCommErrorLed(bool error) {
    digitalWrite(yellowLedPin, error ? HIGH : LOW);
}

// ---------------- WiFi ----------------

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(onBoardLedPin, LOW);
        delay(300);
        Serial.print(".");
    }

    digitalWrite(onBoardLedPin, HIGH);
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void ensureWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        digitalWrite(onBoardLedPin, LOW);
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
            setCommErrorLed(true);
            Serial.print("JSON parse error: ");
            Serial.println(err.c_str());
            return;
        }
        
        setCommErrorLed(false);
        // Expecting: { "state": true } or { "state": false }
        bool state = doc["state"] | false;
        setStateLeds(state);
    } else {
        setCommErrorLed(true);
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
    doc["state"] = state;

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);

    Serial.print("POST ");
    Serial.print(state);
    Serial.print(" -> ");
    Serial.println(code);

    // Communication success = 200 OK
    if (code == 200) {
        setCommErrorLed(false);   // turn OFF yellow LED
    } else {
        setCommErrorLed(true);    // turn ON yellow LED
    }

    http.end();
}


// ---------------- Receive broadcast from server ----------------

void handleLedUpdate() {
    String body = server.arg("plain");
    Serial.println("Received POST: " + body);

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        setCommErrorLed(true);
        Serial.print("JSON parse error: ");
        Serial.println(err.c_str());
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    setCommErrorLed(false);

    const char* state = doc["state"];

    if (strcmp(state, "on") == 0) {
        setStateLeds(true);
        Serial.println("LED set to ON");
    } 
    else if (strcmp(state, "off") == 0) {
        setStateLeds(false);
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
    pinMode(onBoardLedPin, OUTPUT);
    pinMode(greenLedPin, OUTPUT);
    pinMode(yellowLedPin, OUTPUT);
    pinMode(redLedPin, OUTPUT);

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

    // RED button (buttonOff)
    if (lastOffState == HIGH && offState == LOW && (now - lastOffPress > debounceDelay)) {
        setStateLeds(true);                // turn LED ON
        sendStateToServer("on");           // tell Flask to START timer
        lastOffPress = now;
        Serial.println("Red button pressed (TIMER ON)...");
    }

    // GREEN button (buttonOn)
    if (lastOnState == HIGH && onState == LOW && (now - lastOnPress > debounceDelay)) {
        setStateLeds(false);               // turn LED OFF
        sendStateToServer("off");          // tell Flask to STOP timer
        lastOnPress = now;
        Serial.println("Green button pressed (TIMER OFF)...");
    }


    // Save states for next loop
    lastOnState  = onState;
    lastOffState = offState;
}
