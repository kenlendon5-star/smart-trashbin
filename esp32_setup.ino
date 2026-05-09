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
const char* WIFI_SSID = "Bounanotte123(2.4G)";
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

// Bin level update
unsigned long lastBinLevelPush = 0;
const unsigned long BIN_LEVEL_PUSH_INTERVAL_MS = 500;

// Lid open counter sync (increment when lid LED steady-on)
unsigned long lastFirebasePush = 0;
const unsigned long FIREBASE_PUSH_INTERVAL_MS = 1000;

// Track lid LED transitions (exclude blink mode)
bool lastLidLedSteadyOn = false;
unsigned long lidLedLastChangeMs = 0;


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

bool signupOK = false;

void firebaseInitAndStream() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if(Firebase.signUp(&config, &auth, "", "")){
    Serial.println("signup OK");
    signupOK = true;
  }else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;

  // For RTDB, no user/password required if rules allow it.
 
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Stream-based callbacks are not available in this Arduino Firebase client version.
  // We'll poll trashbin/override in loop() instead.

  Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");
  Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", 0);
  Firebase.RTDB.setInt(&fbdo, "trashbin/binLevel", 0);
  Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "closed");
  Serial.println("Firebase ready; polling trashbin/override in loop().");
}

void connectToWifi(){
  Serial.print("Connecting to Wifi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  // 1) Serial
  Serial.begin(115200);

  // 2) WiFi connect
  connectToWifi();
  

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
  // 0) Poll override command (rate-limited)
  static unsigned long lastOverridePoll = 0;
  const unsigned long OVERRIDE_POLL_MS = 250;

  if (!overrideInProgress && millis() - lastOverridePoll >= OVERRIDE_POLL_MS) {
    lastOverridePoll = millis();

    if (Firebase.RTDB.getString(&fbdo, OVERRIDE_PATH)) {
      String val = fbdo.stringData();
      if (val == "open") {
        // Only count when we actually transition from closed->open
        if (!lidOpen) {
          lidOpen = true;
          moveLid(OPEN_DIR);

          int current = 0;
          if (Firebase.RTDB.getInt(&fbdo, "trashbin/lidOpenCount")) {
            current = fbdo.intData();
          }
          current++;
          Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", current);
        } else {
          // Already open; just ensure override is cleared
          // (prevents repeated increments when the command is polled)
        }

        Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "open");
        Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");
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

  // Update binLevel for the web dashboard
  // Map fullDist (10cm = full => 100%) to (20cm or more = empty => 0%)
  // Clamp to [0..100]. Use 999 (out of range) as empty.
  int levelPercent = 0;
  if (fullDist >= 999) {
    levelPercent = 0;
  } else {
    // progress increases as fullDist decreases
    long span = (long)TRIGGER_DISTANCE - (long)FULL_DISTANCE;
    if (span <= 0) span = 1;
    long clamped = fullDist;
    if (clamped < FULL_DISTANCE) clamped = FULL_DISTANCE;
    if (clamped > TRIGGER_DISTANCE) clamped = TRIGGER_DISTANCE;

    // 0..100 where fullDist==FULL_DISTANCE => 100, fullDist==TRIGGER_DISTANCE => 0
    levelPercent = (int)(( (TRIGGER_DISTANCE - clamped) * 100L ) / span);
    if (levelPercent < 0) levelPercent = 0;
    if (levelPercent > 100) levelPercent = 100;
  }

  if (millis() - lastBinLevelPush >= BIN_LEVEL_PUSH_INTERVAL_MS) {
    lastBinLevelPush = millis();
    Firebase.RTDB.setInt(&fbdo, "trashbin/binLevel", levelPercent);
  }

  // Keep lid status synced for the web UI
  // (avoid writing too frequently; only update when behavior changes is ideal,
  // but this is simple and still rate-limited by our loop delay).
  // lidOpen true => open else closed.
  if (lidOpen) {
    Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "open");
  } else {
    Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "closed");
  }

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
  bool ledSteadyOn = false;
  if (binFull) {
    if (millis() - lastBlink >= 100) {
      lastBlink = millis();
      digitalWrite(ledPin, !digitalRead(ledPin));
    }
  } else if (personDetected) {
    ledSteadyOn = true;
    digitalWrite(ledPin, HIGH);
  } else if (spraying) {
    if (millis() - lastBlink >= 300) {
      lastBlink = millis();
      digitalWrite(ledPin, !digitalRead(ledPin));
    }
  } else {
    digitalWrite(ledPin, LOW);
  }

  // Increment lidOpenCount whenever the blue LED turns on steadily (not blinking)
  if (ledSteadyOn && !lastLidLedSteadyOn) {
    if (millis() - lidLedLastChangeMs > 1000) {
      lidLedLastChangeMs = millis();
      int current = 0;
      if (Firebase.RTDB.getInt(&fbdo, "trashbin/lidOpenCount")) {
        current = fbdo.intData();
      }
      current++;
      Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", current);
    }
  }
  lastLidLedSteadyOn = ledSteadyOn;

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