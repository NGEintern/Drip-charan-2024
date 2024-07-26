#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// WiFi credentials
const char* ssid = "NGE_Airtel";
const char* password = "Nge@12345";
const char* serverUrl = "http://192.168.1.10:5000";

// GPIO pins for motors and flow sensors
const int motorPins[] = {2, 3, 4, 5};
const int flowSensorPins[] = {27, 25, 26, 14};
float flowRates[4] = {0.0, 0.0, 0.0, 0.0};

// Variables for motor status
String motorStatus[4] = {"OFF", "OFF", "OFF", "OFF"};

// Preferences for storing schedules
Preferences prefs;

void setup() {
  Serial.begin(115200);
  
  // Connect to WiFi
  connectWifi(ssid, password);

  // Initialize motor pins
  for (int pin : motorPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  // Initialize flow sensor pins
  for (int pin : flowSensorPins) {
    pinMode(pin, INPUT);
  }

  // Initialize preferences
  prefs.begin("schedules", false);

  Serial.println("Setup complete");
}

void loop() {
  Serial.println("Loop started");

  // Check motor statuses and update accordingly
  for (int i = 0; i < 4; i++) {
    checkMotorStatus(i + 1);
  }

  // Update flow sensor data
  updateFlowSensorData();

  // Send flow sensor data to the server
  sendFlowSensorData();

  // Send heartbeat to the server
  sendHeartbeat();

  // Receive and store schedules from the server
  receiveSchedules();

  delay(5000); // Adjust the delay as needed
}

void connectWifi(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void checkMotorStatus(int motorId) {
  HTTPClient http;
  http.begin(String(serverUrl) + "/motor/status/" + motorId);
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Motor " + String(motorId) + " status: " + payload);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    if (doc["status"] == "ON") {
      digitalWrite(motorPins[motorId - 1], HIGH);
      motorStatus[motorId - 1] = "ON";
    } else {
      digitalWrite(motorPins[motorId - 1], LOW);
      motorStatus[motorId - 1] = "OFF";
    }
  } else {
    Serial.print("Error on sending request: ");
    Serial.println(httpCode);
  }
  http.end();
}

void updateFlowSensorData() {
  for (int i = 0; i < 4; i++) {
    flowRates[i] = analogRead(flowSensorPins[i]); // Replace with actual reading logic
    Serial.println("Flow Rate " + String(i+1) + ": " + String(flowRates[i]));
  }
}

void sendFlowSensorData() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/update_flow");
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1024);
  doc["flowRate1"] = flowRates[0];
  doc["flowRate2"] = flowRates[1];
  doc["flowRate3"] = flowRates[2];
  doc["flowRate4"] = flowRates[3];

  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpCode = http.POST(jsonString);
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Flow update response: " + payload);
  } else {
    Serial.print("Error on sending request: ");
    Serial.println(httpCode);
  }
  http.end();
}

void sendHeartbeat() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/heartbeat");
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    Serial.println("Heartbeat sent successfully");
  } else {
    Serial.println("Error sending heartbeat");
  }
  http.end();
}

void receiveSchedules() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/get_schedules");
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println(httpResponseCode);
    Serial.println(response);

    // Parse JSON response and store schedules locally
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      return;
    }

    // Store schedules in Preferences or other storage
    prefs.clear();
    JsonObject schedules = doc.as<JsonObject>();
    for (JsonPair schedule : schedules) {
      prefs.putString(schedule.key().c_str(), schedule.value().as<String>());
      Serial.print("Stored schedule for ");
      Serial.print(schedule.key().c_str());
      Serial.print(": ");
      Serial.println(schedule.value().as<String>());
    }
  } else {
    Serial.print("Error on receiving schedules: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}
