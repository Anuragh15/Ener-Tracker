#define BLYNK_TEMPLATE_ID "TMPL3ikVwtGff"
#define BLYNK_TEMPLATE_NAME "IoT Energy Meter"
#define BLYNK_PRINT Serial

#include "EmonLib.h"
#include <EEPROM.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// LCD Display (ESP32-Compatible)
LiquidCrystal_PCF8574 lcd(0x27);

// Telegram Bot credentials
const char* telegramBotToken = "*********";  // Replace with your actual bot token
const char* telegramChatID = "*********";  // Replace with your actual chat ID

// Calibration constants
const float vCalibration = 42.5;
const float currCalibration = 1.80;

// Blynk and WiFi credentials
const char auth[] = "6Qvuuvb8SBU5Ycl7RTSl9iQ7yrKh16sS";
const char ssid[] = "Mnr";
const char pass[] = "12345678";

// EnergyMonitor instance
EnergyMonitor emon;

// Blynk timer
BlynkTimer timer;

// Energy tracking variables
float kWh = 0.0;
float cost = 0.0;
const float ratePerkWh = 6.5;
unsigned long lastMillis = millis();

// EEPROM storage addresses
const int addrKWh = 12;
const int addrCost = 16;

// LCD display page
int displayPage = 0;

// Reset button and PIR sensor pins
const int resetButtonPin = 4;
const int pirSensorPin = 5;  // PIR sensor connected to GPIO 5

// PIR Sensor state
bool motionDetected = false;

// Function prototypes
void sendEnergyDataToBlynk();
void readEnergyDataFromEEPROM();
void saveEnergyDataToEEPROM();
void updateLCD();
void changeDisplayPage();
void sendBillToTelegram();
void resetEEPROM();
void handleMotionDetection();

void setup() {
  Serial.begin(115200);
  
  // Initialize WiFi
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  Blynk.begin(auth, ssid, pass);

  // Initialize LCD
  lcd.begin(16, 2);
  lcd.setBacklight(255);
  lcd.setCursor(0, 0);
  lcd.print("IoT Energy Meter");
  delay(2000);
  lcd.clear();

  // Initialize EEPROM
  EEPROM.begin(32);

  // Initialize buttons and sensors
  pinMode(resetButtonPin, INPUT_PULLUP);
  pinMode(pirSensorPin, INPUT);

  // Read stored energy data
  readEnergyDataFromEEPROM();

  // Setup energy monitor
  emon.voltage(35, vCalibration, 1.7);
  emon.current(34, currCalibration);

  // Setup timers
  timer.setInterval(2000L, sendEnergyDataToBlynk);
  timer.setInterval(2000L, changeDisplayPage);
  timer.setInterval(60000L, sendBillToTelegram);
  timer.setInterval(1000L, handleMotionDetection); // Check PIR sensor every second
}

void loop() {
  Blynk.run();
  timer.run();

  // Check if reset button is pressed
  if (digitalRead(resetButtonPin) == LOW) {
    delay(200);
    resetEEPROM();
  }
}

void sendEnergyDataToBlynk() {
  emon.calcVI(20, 2000);
  float Vrms = emon.Vrms;
  float Irms = emon.Irms;
  float apparentPower = emon.apparentPower;

  // Calculate energy consumption
  unsigned long currentMillis = millis();
  kWh += apparentPower * (currentMillis - lastMillis) / 3600000000.0;
  lastMillis = currentMillis;

  // Calculate cost
  cost = kWh * ratePerkWh;

  // Save updated data
  saveEnergyDataToEEPROM();

  // Send data to Blynk
  Blynk.virtualWrite(V0, Vrms);
  Blynk.virtualWrite(V1, Irms);
  Blynk.virtualWrite(V2, apparentPower);
  Blynk.virtualWrite(V3, kWh);
  Blynk.virtualWrite(V4, cost);

  // Update LCD display
  updateLCD();
}

void readEnergyDataFromEEPROM() {
  EEPROM.get(addrKWh, kWh);
  EEPROM.get(addrCost, cost);

  if (isnan(kWh)) {
    kWh = 0.0;
    saveEnergyDataToEEPROM();
  }
  if (isnan(cost)) {
    cost = 0.0;
    saveEnergyDataToEEPROM();
  }
}

void saveEnergyDataToEEPROM() {
  EEPROM.put(addrKWh, kWh);
  EEPROM.put(addrCost, cost);
  EEPROM.commit();
}

void updateLCD() {
  lcd.clear();
  if (displayPage == 0) {
    lcd.setCursor(0, 0);
    lcd.printf("V:%.fV I:%.fA", emon.Vrms, emon.Irms);
    lcd.setCursor(0, 1);
    lcd.printf("P: %.f Watt", emon.apparentPower);
  } else if (displayPage == 1) {
    lcd.setCursor(0, 0);
    lcd.printf("Energy: %.2fkWh", kWh);
    lcd.setCursor(0, 1);
    lcd.printf("Cost: %.2f", cost);
  }
}

void changeDisplayPage() {
  displayPage = (displayPage + 1) % 2;
  updateLCD();
}

void sendBillToTelegram() {
  String message = "Total Energy Consumed: " + String(kWh, 2) + " kWh\nTotal Cost: ₹" + String(cost, 2);

  HTTPClient http;
  http.begin("https://api.telegram.org/bot" + String(telegramBotToken) + "/sendMessage");
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument jsonDoc(256);
  jsonDoc["chat_id"] = telegramChatID;
  jsonDoc["text"] = message;

  String jsonString;
  serializeJson(jsonDoc, jsonString);
  int httpCode = http.POST(jsonString);
  
  http.end();
}

void resetEEPROM() {
  kWh = 0.0;
  cost = 0.0;
  saveEnergyDataToEEPROM();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Data Reset!");
  delay(2000);
}

// Motion Detection Function
void handleMotionDetection() {
  int motionState = digitalRead(pirSensorPin);

  if (motionState == HIGH && !motionDetected) {
    motionDetected = true;
    Serial.println("Motion Detected!");

    // Send motion alert to Blynk
    Blynk.virtualWrite(V5, 1);

    // Send alert to Telegram
    String motionMessage = "🚨 Motion detected!";
    HTTPClient http;
    http.begin("https://api.telegram.org/bot" + String(telegramBotToken) + "/sendMessage");
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument jsonDoc(256);
    jsonDoc["chat_id"] = telegramChatID;
    jsonDoc["text"] = motionMessage;

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    http.POST(jsonString);
    http.end();
  } 
  
  if (motionState == LOW && motionDetected) {
    motionDetected = false;
    Blynk.virtualWrite(V5, 0);
  }
}
