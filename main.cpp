/*
  Joke Machine - Final Robust Version
  Feature:
  1. Make.com Proxy for fetching jokes (Receives Pure Text)
  2. Infinite Retry Logic for BOTH fetching and sending logs
  3. Servo & Buzzer Reaction by Rating (1~5)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Keypad.h>
#include <ESP32Servo.h>

// =====================================================
// WiFi
// =====================================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// =====================================================
// Make.com Webhooks
// =====================================================
String MAKE_JOKE_URL = "https://hook.eu1.make.com/zy7b0hejxuo8et7phv8spy09pi0jlm1v";
String MAKE_LOG_URL  = "https://hook.eu1.make.com/oe62icuevcowayxvinrny6xah977xou3";

// =====================================================
// TFT LCD
// =====================================================
#define TFT_DC   2
#define TFT_CS   15
#define TFT_RST  4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// Keypad
// =====================================================
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {27, 26, 25, 33};
byte colPins[COLS] = {32, 17, 16, 22};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// =====================================================
// Buzzer & Servo (diagram 기준)
// =====================================================
const int BUZZER_PIN = 14;

const int SERVO1_PIN = 12;   // upper servo
const int SERVO2_PIN = 13;   // lower servo

Servo servo1;
Servo servo2;

const int SERVO_CENTER = 90;

// =====================================================
// State
// =====================================================
enum MachineState {
  STATE_MENU,
  STATE_RATING
};

MachineState currentState = STATE_MENU;

// =====================================================
// Global Data
// =====================================================
String currentCategory = "";
String currentJoke = "";

// =====================================================
// Utility
// =====================================================
void beep(int f, int d, int p) {
  tone(BUZZER_PIN, f);
  delay(d);
  noTone(BUZZER_PIN);
  delay(p);
}

// 점수 문자 → 정수
int normalizeScore(char scoreChar) {
  int s = scoreChar - '0';
  if (s < 1) s = 1;
  if (s > 5) s = 5;
  return s;
}

// =====================================================
// Servo Motion by Score
// =====================================================
void servoLaughMotion(char scoreChar) {
  int score = normalizeScore(scoreChar);
  int amplitude = map(score, 1, 5, 10, 60);
  int repeat    = map(score, 1, 5, 2, 5);

  for (int i = 0; i < repeat; i++) {
    servo1.write(SERVO_CENTER + amplitude);
    servo2.write(SERVO_CENTER - amplitude);
    delay(120);

    servo1.write(SERVO_CENTER - amplitude);
    servo2.write(SERVO_CENTER + amplitude);
    delay(120);
  }

  servo1.write(SERVO_CENTER);
  servo2.write(SERVO_CENTER);
}

// =====================================================
// Buzzer Pattern by Score
// =====================================================
void buzzerLaugh(char scoreChar) {
  int score = normalizeScore(scoreChar);

  switch (score) {
    case 1:
      beep(700, 120, 200);
      break;

    case 2:
      beep(800, 120, 150);
      beep(800, 120, 150);
      break;

    case 3:
      for (int i = 0; i < 3; i++) beep(1000, 120, 100);
      break;

    case 4:
      beep(900, 120, 80);
      beep(1100, 120, 80);
      beep(1300, 120, 80);
      beep(1500, 150, 120);
      break;

    case 5:
      for (int i = 0; i < 6; i++) beep(1600, 100, 60);
      beep(1800, 200, 120);
      break;
  }
}

// =====================================================
// Fetch Joke from Make (Retry handled outside)
// =====================================================
String getJokeFromMake(String category) {
  if (WiFi.status() != WL_CONNECTED) return "Error: WiFi";

  WiFiClientSecure* client = new WiFiClientSecure;
  client->setInsecure();
  client->setHandshakeTimeout(20000);

  HTTPClient http;
  String url = MAKE_JOKE_URL + "?category=" + category;

  String result = "Error";

  if (http.begin(*client, url)) {
    int httpCode = http.GET();
    if (httpCode > 0) result = http.getString();
    http.end();
  }

  delete client;
  return result;
}

// =====================================================
// Send Rating Log
// =====================================================
bool sendLogToMake(String category, String joke, int rating) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure* client = new WiFiClientSecure;
  client->setInsecure();
  client->setHandshakeTimeout(20000);

  HTTPClient http;
  bool success = false;

  if (http.begin(*client, MAKE_LOG_URL)) {
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<1024> doc;
    doc["category"] = category;
    doc["joke"]     = joke;
    doc["rating"]   = rating;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    if (code > 0) success = true;

    http.end();
  }

  delete client;
  return success;
}

// =====================================================
// UI Logic
// =====================================================
void showMenu() {
  currentState = STATE_MENU;
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.println("\nSelect Category:");
  tft.println("1:Misc 2:Prog");
  tft.println("3:Dark 4:Pun");
  tft.println("5:Spooky 6:X-mas");
  tft.println("7:Any");
}

// =====================================================
// Show Joke
// =====================================================
void nextJoke(String category) {
  currentCategory = category;

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_WHITE);
  tft.println("Fetching Joke...");

  String data = getJokeFromMake(category);
  while (data.startsWith("Error")) {
    delay(2000);
    data = getJokeFromMake(category);
  }

  currentJoke = data;

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_GREEN);
  tft.println(currentJoke);

  tft.setTextColor(ILI9341_MAGENTA);
  tft.println("\n--------------------");
  tft.println("Rate this joke (1-5)");

  currentState = STATE_RATING;
}

// =====================================================
// Rating Handler (Servo + Buzzer HERE)
// =====================================================
void showRatingThankYou(char score) {
  int rating = score - '0';

  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.printf("Rating: %d/5\n", rating);

  // ⭐ 핵심 반응
  servoLaughMotion(score);
  buzzerLaugh(score);

  while (!sendLogToMake(currentCategory, currentJoke, rating)) {
    delay(2000);
  }

  tft.println("Saved!");
  delay(1500);
  showMenu();
}


// =====================================================
// Setup
// =====================================================
void setup() {
  Serial.begin(115200);

  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(SERVO_CENTER);
  servo2.write(SERVO_CENTER);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password, 6);

  tft.begin();
  tft.setRotation(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    tft.print(".");
  }

  showMenu();
}

// =====================================================
// Loop
// =====================================================
void loop() {
  char key = keypad.getKey();

  if (key != NO_KEY) {
    if (currentState == STATE_MENU) {
      String cat = "";
      switch (key) {
        case '1': cat = "Misc"; break;
        case '2': cat = "Programming"; break;
        case '3': cat = "Dark"; break;
        case '4': cat = "Pun"; break;
        case '5': cat = "Spooky"; break;
        case '6': cat = "Christmas"; break;
        case '7': cat = "Any"; break;
      }
      if (cat.length()) nextJoke(cat);
    }
    else if (currentState == STATE_RATING) {
      if (key >= '1' && key <= '5') showRatingThankYou(key);
      else if (key == '*') showMenu();
    }
  }
  delay(10);
}
