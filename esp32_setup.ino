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
const char* WIFI_SSID = "Regex";
const char* WIFI_PASSWORD = "Regex314";


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

// Lid close timing
const unsigned long LID_CLOSE_DELAY_MS = 600; // require no-person for this long before closing


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

// NOTE: ensure these debug flags exist at compile time (kept for future tuning)
// bool lastSeenOverride = false;


// ================= STATES =================
// NOTE: use a debounced/hysteresis version of the "person present" signal
// to prevent the lid from getting stuck open due to brief ultrasonic glitches.
bool personDetected = false; // stable/debounced value used by lid logic
bool lidOpen = false;
bool binFull = false;

// Person detection tuning
const long PERSON_LOST_DELAY_MS = 1500; // must be absent for this long before we allow closing
// const long PERSON_PRESENT_MARGIN_CM = 2; // (unused) kept for future hysteresis tuning
bool personDetectedRaw = false;
unsigned long personLastSeenMs = 0;
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
const unsigned long BIN_LEVEL_PUSH_INTERVAL_MS = 2000; // less write churn to Firebase
const int BIN_LEVEL_DEADBAND = 3; // only push if change >= this amount
int lastPushedBinLevel = -1;


// Lid open counter (count every time lid opens from closed -> open)
bool lastLidOpenState = false;
unsigned long lidOpenEdgeCooldownMs = 800; // debounce
unsigned long lastLidOpenEdgeMs = 0;

int lastSeenOverride = -1; // not used, placeholder if you want debounce later


String lastOverrideValue = "";
bool overrideInProgress = false;

// Manual override: when user requests "open", we should not let sensor logic immediately close it.
bool manualOverrideActive = false;
unsigned long manualOverrideOpenedAtMs = 0;
const unsigned long MANUAL_OVERRIDE_TIMEOUT_MS = 30000; // 30s; prevents staying open forever.

// auto-close helper: start close timer when no person detected
bool closeTimerArmed = false;
unsigned long closeTimerStartMs = 0;


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

  // Don't block forever: allow the device to run sensors even if internet/Firebase is down.
  const unsigned long WIFI_TIMEOUT_MS = 15000;
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi not connected (continuing local control only)");
  }
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
  lastLidOpenState = false;
}

void loop() {
  // 0) Poll override command (rate-limited)
  static unsigned long lastOverridePoll = 0;
  const unsigned long OVERRIDE_POLL_MS = 250;

  // Keep last pushed level initialized if first run
  static bool initializedBinDeadband = false;
  if (!initializedBinDeadband) {
    lastPushedBinLevel = -1;
    initializedBinDeadband = true;
  }





  if (!overrideInProgress && millis() - lastOverridePoll >= OVERRIDE_POLL_MS) {
    lastOverridePoll = millis();

    if (Firebase.RTDB.getString(&fbdo, OVERRIDE_PATH)) {
      String val = fbdo.stringData();
      if (val == "open") {
        // Manual open override: keep lid from auto-closing until timeout or explicit close.
        manualOverrideActive = true;
        manualOverrideOpenedAtMs = millis();

  // If currently closed, transition to open and count.
        if (!lidOpen) {
          lidOpen = true;
          moveLid(OPEN_DIR);

          // Count every closed -> open transition (debounced)
          unsigned long nowMs = millis();
          if (nowMs - lastLidOpenEdgeMs >= lidOpenEdgeCooldownMs) {
            int current = 0;
            if (Firebase.RTDB.getInt(&fbdo, "trashbin/lidOpenCount")) {
              current = fbdo.intData();
            }
            current++;
            Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", current);
            lastLidOpenEdgeMs = nowMs;
          }
        }



        Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", "open");
        Firebase.RTDB.setString(&fbdo, OVERRIDE_PATH, "none");
      } else if (val == "close") {
        overrideInProgress = true;
        manualOverrideActive = false;
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

// Person detection with hysteresis + consecutive no-person samples
  // Open threshold: <= TRIGGER_DISTANCE
  const int CLOSE_DISTANCE = TRIGGER_DISTANCE + 5; // close only when farther than open threshold


  bool personNow = (dist <= TRIGGER_DISTANCE);      // “someone is here”
  bool personFar = (dist > CLOSE_DISTANCE);        // “confidently gone”

  // Keep lastSeen for manual override timeout/debug if needed
  if (personNow) {
    personLastSeenMs = millis();
  }

  // Count consecutive confident no-person readings
  const int NO_PERSON_SAMPLE_COUNT = 3;
  static int noPersonStreak = 0;
  if (personFar) {
    noPersonStreak++;
  } else {
    noPersonStreak = 0;
  }

  // Final debounced “person present” state:
  // - stay present for any near detection
  // - require N consecutive far readings before allowing “no person”
  personDetectedRaw = personNow;
  bool personDetectedByStreak = (noPersonStreak < NO_PERSON_SAMPLE_COUNT);
  personDetected = personDetectedByStreak;



  // If manual override is active, keep the lid open and suppress automatic closing.
  if (manualOverrideActive) {
    if (millis() - manualOverrideOpenedAtMs >= MANUAL_OVERRIDE_TIMEOUT_MS) {
      manualOverrideActive = false;
    }
  }

  // Arm auto-close timer only when we have a stable "no person" state.
  // This prevents the lid from getting stuck open during intermittent sensor gaps.
  if (!personDetected) {
    if (!closeTimerArmed) {
      closeTimerArmed = true;
      closeTimerStartMs = millis();
    }
  } else {
    closeTimerArmed = false;
  }



  // Update binLevel for the web dashboard
  // fullDist is the distance from the bin's "fullness" sensor.
  // Goal: FULL_DISTANCE -> 100%, TRIGGER_DISTANCE -> 0%.
  // Note: if the sensor fails (999), treat as empty (0%).
  int levelPercent = 0;

  // Map distance -> fullness % using stable linear interpolation.
  // When the bin is EMPTY: fullDist should be >= TRIGGER_DISTANCE => 0%
  // When the bin is FULL : fullDist should be <= FULL_DISTANCE    => 100%
  if (fullDist >= 999) {
    levelPercent = 0;
  } else {
    long d = fullDist;
    if (d <= FULL_DISTANCE) d = FULL_DISTANCE;
    if (d >= TRIGGER_DISTANCE) d = TRIGGER_DISTANCE;

    long range = (long)TRIGGER_DISTANCE - (long)FULL_DISTANCE; // positive
    if (range <= 0) range = 1;

    // fraction = (TRIGGER - d) / (TRIGGER - FULL)  => 0..1
    long fractionNumerator = (long)TRIGGER_DISTANCE - d; // 0..range
    levelPercent = (int)(fractionNumerator * 100L / range);

    if (levelPercent < 0) levelPercent = 0;
    if (levelPercent > 100) levelPercent = 100;
  }


  if (millis() - lastBinLevelPush >= BIN_LEVEL_PUSH_INTERVAL_MS) {
    // Deadband: reduce RTDB churn when small sensor noise occurs
    if (abs(levelPercent - lastPushedBinLevel) >= BIN_LEVEL_DEADBAND) {
      lastBinLevelPush = millis();
      Firebase.RTDB.setInt(&fbdo, "trashbin/binLevel", levelPercent);
      lastPushedBinLevel = levelPercent;
    }
  }


  // Keep lid status synced for the web UI, but only when it changes
  static bool lastPushedLidOpen = false;
  if (lidOpen != lastPushedLidOpen) {
    lastPushedLidOpen = lidOpen;
    Firebase.RTDB.setString(&fbdo, "trashbin/lidStatus", lidOpen ? "open" : "closed");
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




  // 4. CORE BEHAVIOR

  // If manual override is active, keep lid open and skip the automatic close/reopen logic.
  if (manualOverrideActive && lidOpen) {
    digitalWrite(relayPin, HIGH); // keep relay off (active LOW) during manual override
    spraying = false;
    waitingToSpray = false;
  } else if (binFull) {

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

      // Count every closed -> open transition (sensor-driven) (debounced)
      unsigned long nowMs = millis();
      if (nowMs - lastLidOpenEdgeMs >= lidOpenEdgeCooldownMs) {
        int current = 0;
        if (Firebase.RTDB.getInt(&fbdo, "trashbin/lidOpenCount")) {
          current = fbdo.intData();
        }
        current++;
        Firebase.RTDB.setInt(&fbdo, "trashbin/lidOpenCount", current);
        lastLidOpenEdgeMs = nowMs;
      }
    }
  } 
  
  else {
    // If lid is open, only close after we've had stable "no person" for a bit.
    if (lidOpen) {
      if (closeTimerArmed && (millis() - closeTimerStartMs >= LID_CLOSE_DELAY_MS)) {
        moveLid(CLOSE_DIR); // Counter-Clockwise Close
        lidOpen = false;
        waitingToSpray = true;
        closeTime = millis();
        closeTimerArmed = false;
      }
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