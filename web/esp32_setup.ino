#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <ESP32Servo.h>

Servo lidServo;

// ================= FIREBASE =================
#define API_KEY "AIzaSyA-lRaDMrUtaONmTth7YZXOCj0qWhqLKwQ"
#define DATABASE_URL "https://trashbin-esp32-default-rtdb.asia-southeast1.firebasedatabase.app"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;
FirebaseStream stream;

// ================= WIFI (EDIT THESE) =================
const char* WIFI_SSID = "Bounanotte123";
const char* WIFI_PASSWORD = "zT7$kP9!wX@2vLmQ#f3RGv9z!LmQ8^rT@2Xp$d7*BfJw3&eK4Nh";

// ================= PINS =================
const int trigPin = 19;
const int echoPin = 18;
const int trigPinFull = 21;
const int echoPinFull = 22;
const int servoPin = 13;
const int ledPin = 2;
const int relayPin = 5;

// ================= SETTINGS =================
const int TRIGGER_DISTANCE = 20;
const int FULL_DISTANCE = 10;
const int STOP = 90; // Neutral (No movement)

// REVERSED DIRECTIONS
const int OPEN_DIR = 0;     // Clockwise rotation to OPEN
const int CLOSE_DIR = 180;  // Counter-Clockwise rotation to CLOSE

// TIMING FOR 90 DEGREES
const int LID_MOVEMENT_TIME = 350;

const unsigned long SPRAY_DELAY = 3000;
const unsigned long SPRAY_DURATION = 1000;

// ================= FIREBASE PATHS =================
const char* TRASH_PATH = "trashbin";
const char* OVERRIDE_PATH = "trashbin/override";

// ================= STATES =================
bool personDetected = false;
bool lidOpen = false;
bool binFull = false;
bool spraying = false;
bool waitingToSpray = false;

unsigned long closeTime = 0;
unsigned long sprayStartTime = 0;
unsigned long lastBlink = 0;

int fullCount = 0;
int emptyCount = 0;
const int FULL_TRIGGER_COUNT = 5;

// Lid open counter sync
unsigned long lastFirebasePush = 0;
const unsigned long FIREBASE_PUSH_INTERVAL_MS = 1000;

int lastSeenOverride = -1; // not used, placeholder if you want debounce later

String lastOverrideValue = "";
bool overrideInProgress = false;

// ================= SENSOR HELPER =================
long readDistance(int tp, int ep) {
  digitalWrite(tp, LOW);
  delayMicroseconds(2);
  digitalWrite(tp, HIGH);
  delayMicroseconds(10);
  digitalWrite(tp, LOW);
  long duration = pulseIn(ep, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ================= MOVEMENT HELPER =================
void moveLid(int dir) {
  lidServo.write(dir);      
  delay(LID_MOVEMENT_TIME); 
  lidServo.write(STOP);     
}

void firebaseInitAndStream() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // For RTDB, no user/password required if rules allow it.
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);

  // Stream-based callbacks are not available in this Arduino Firebase client version.
  // We'll poll trashbin/override in loop() instead.

  Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");
  Serial.println("Firebase ready; polling trashbin/override in loop().");
}

void setup() {
  // 1) Serial
  Serial.begin(115200);

  // 2) WiFi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());

  // 3) Firebase
  firebaseInitAndStream();

  // 4) Pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(trigPinFull, OUTPUT);
  pinMode(echoPinFull, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);

  lidServo.attach(servoPin);
  lidServo.write(STOP);

  digitalWrite(relayPin, HIGH); // Relay OFF (Active LOW)
  digitalWrite(ledPin, LOW);

  lidOpen = false;
  binFull = false;
}

void loop() {
  // 0) Poll override command
  if (!overrideInProgress) {
    if (Firebase.RTDB.getString(&fbdo, OVERRIDE_PATH)) {
      String val = fbdo.stringData();
      if (val == "open") {
        overrideInProgress = true;
        lidOpen = true;
        moveLid(OPEN_DIR);

        // bump lidOpenCount
        int current = 0;
        if (Firebase.RTDB.getInt(&fbdo, "trashbin/lidOpenCount")) {
          current = fbdo.intData();
        }
        current++;
        Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", current);
        Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "open");
        Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");

        overrideInProgress = false;
      } else if (val == "close") {
        overrideInProgress = true;
        lidOpen = false;
        moveLid(CLOSE_DIR);
        Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "closed");
        Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");
        overrideInProgress = false;
      }
    }
  }

  // 1. READ SENSORS
  long dist = readDistance(trigPin, echoPin);
  long fullDist = readDistance(trigPinFull, echoPinFull);

  personDetected = (dist <= TRIGGER_DISTANCE);

  // 2. BIN FULL LOGIC

  if (fullDist <= FULL_DISTANCE) {
    fullCount++;
    emptyCount = 0;
  } else {
    emptyCount++;
    fullCount = 0;
  }
  if (fullCount >= FULL_TRIGGER_COUNT) binFull = true;
  if (emptyCount >= FULL_TRIGGER_COUNT) binFull = false;

  // 3. LED FEEDBACK
  if (binFull) {
    if (millis() - lastBlink >= 100) {
      lastBlink = millis();
      digitalWrite(ledPin, !digitalRead(ledPin));
    }
  } else if (personDetected) {
    digitalWrite(ledPin, HIGH);
  } else if (spraying) {
    if (millis() - lastBlink >= 300) {
      lastBlink = millis();
      digitalWrite(ledPin, !digitalRead(ledPin));
    }
  } else {
    digitalWrite(ledPin, LOW);
  }

  // 4. CORE BEHAVIOR
  
  if (binFull) {
    if (lidOpen) {
      moveLid(CLOSE_DIR); // Reverse back
      lidOpen = false;
    }
    digitalWrite(relayPin, HIGH);
    spraying = false;
    waitingToSpray = false;
  } 
  
  else if (personDetected) {
    digitalWrite(relayPin, HIGH); 
    spraying = false;
    waitingToSpray = false;

    if (!lidOpen) {
      moveLid(OPEN_DIR); // Clockwise Open
      lidOpen = true;
    }
  } 
  
  else {
    if (lidOpen) {
      moveLid(CLOSE_DIR); // Counter-Clockwise Close
      lidOpen = false;
      waitingToSpray = true; 
      closeTime = millis();
    }

    if (waitingToSpray) {
      if (millis() - closeTime >= SPRAY_DELAY) {
        waitingToSpray = false;
        spraying = true;
        sprayStartTime = millis();
        digitalWrite(relayPin, LOW); 
      }
    }

    if (spraying) {
      if (millis() - sprayStartTime >= SPRAY_DURATION) {
        spraying = false;
        digitalWrite(relayPin, HIGH); 
      }
    }
  }

  delay(50);
} 