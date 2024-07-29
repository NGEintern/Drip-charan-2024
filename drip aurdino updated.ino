#include <WiFi.h>
#include <HTTPClient.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>

// WiFi credentials
const char* serverUrl = "http://192.168.1.10:5000";

// GPIO pins for motors and flow sensors
const int motorPins[] = {2, 3, 4, 5};
const int flowSensorPins[] = {27, 25, 26, 14};
float flowRates[4] = {0.0, 0.0, 0.0, 0.0};
ThreeWire myWire(27, 14, 26); // DAT, SCLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Variables for motor status
String motorStatus[4] = {"OFF", "OFF", "OFF", "OFF"};

// Preferences for storing schedules and credentials
Preferences prefs;
BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("Drip");

  // Initialize RTC and fetch time from the server
  fetchTimeFromServer();

  // Load stored WiFi credentials
  prefs.begin("credentials", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length() > 0 && pass.length() > 0) {
    connectWifi(ssid.c_str(), pass.c_str());
  } else {
    // Wait for Bluetooth credentials
    Serial.println("Waiting for WiFi credentials via Bluetooth...");
    while (true) {
      if (SerialBT.available()) {
        String data = SerialBT.readString();
        if (data.length() > 0) {
          int spaceIndex = data.indexOf(' ');
          if (spaceIndex > 0) {
            String word1 = data.substring(0, spaceIndex);
            String word2 = data.substring(spaceIndex + 1);
            prefs.begin("credentials", false);
            prefs.putString("ssid", word1);
            prefs.putString("pass", word2);
            prefs.end();

            Serial.println("Received SSID: " + word1);
            Serial.println("Received Password: " + word2);

            connectWifi(word1.c_str(), word2.c_str());
            break;
          }
        }
      }
    }
  }

  // Initialize motor pins
  for (int pin : motorPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  // Initialize flow sensor pins
  for (int pin : flowSensorPins) {
    pinMode(pin, INPUT);
  }

  // Initialize preferences for schedules
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

void fetchTimeFromServer() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(serverUrl) + "/fetch-time";

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("Server Response:");
      Serial.println(payload);

      // Parse the JSON response
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      int year = doc["year"];
      int month = doc["month"];
      int day = doc["day"];
      int hour = doc["hour"];
      int minute = doc["minute"];
      int second = doc["second"];

      Serial.printf("Received Time: %d-%d-%d %d:%d:%d\n", year, month, day, hour, minute, second);
      
      // Update RTC with the received time
      RtcDateTime newTime(year, month, day, hour, minute, second);
      Rtc.SetDateTime(newTime);

      Serial.println("RTC Updated with new time.");
    } else {
      Serial.println("Failed to fetch time. HTTP Code: " + String(httpCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void printCurrentTime() {
    RtcDateTime now = Rtc.GetDateTime();
    Serial.print("Current RTC Time: ");
    printDateTime(now);
    Serial.println();
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt) {
    char datestring[20];

    snprintf_P(datestring,
                countof(datestring),
                PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
                dt.Month(),
                dt.Day(),
                dt.Year(),
                dt.Hour(),
                dt.Minute(),
                dt.Second());
    Serial.print(datestring);
}
