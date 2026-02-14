
/*
 * Peer-to-Peer Smart IoT Water Grid - Node A
 * 
 * Hardware:
 * - ESP32 DevKit
 * - Potentiometer 1 (GPIO 34): Initial Tank Level (read once at startup)
 * - Potentiometer 2 (GPIO 35): Pump Current Simulation
 * - LED 1 (GPIO 26): Main Pump
 * - LED 2 (GPIO 27): Backup Pump
 * - I2C LCD 16x2 (SDA 21, SCL 22)
 *
 * Logic:
 * - Mathematical tank level modeling (no continuous analogRead for level)
 * - Decentralized coordination via MQTT
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ================= CONSTANTS & CONFIG =================
const char* SSID = "Wokwi-GUEST";
const char* PASSWORD = "";
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// Unique topics to avoid collision
const char* TOPIC_REQUEST = "watergrid_shubham2026/request";
const char* TOPIC_RESPONSE = "watergrid_shubham2026/response";
const char* TOPIC_STATUS = "watergrid_shubham2026/nodeA/status";

// Node Identity
const char* NODE_ID = "A";
const char* PEER_ID = "B";

// Pins
const int PIN_LEVEL_INIT = 34;   // Potentiometer for startup level only
const int PIN_CURRENT    = 35;   // Potentiometer for pump current
const int PIN_PUMP_MAIN  = 26;   // Main Pump LED
const int PIN_PUMP_BACKUP= 27;   // Backup Pump LED

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Water Grid Parameters
const float MIN_KEEP      = 50.0; // Minimum % to keep before supplying
const float LOW_THRESHOLD = 40.0; // Request water if below this (Old Logic Restored)
const float TARGET_LEVEL  = 70.0; // Aim for this level
const float CRITICAL      = 10.0; // Dry run protection
const float MAX_LEVEL     = 100.0;

// ================= GLOBAL VARIABLES =================
// Internal State
float tankLevel = 0.0;
bool waitingForResponse = false;
unsigned long lastRequestTime = 0;
const long REQUEST_INTERVAL = 5000; // Retry request every 5s if needed
int lastPotRaw = -1; // Track last potentiometer reading
unsigned long lastTelemetry = 0; // For dashboard updates

enum State {
  STATE_IDLE,
  STATE_REQUESTING,
  STATE_SUPPLYING,
  STATE_RECEIVING,
  STATE_DENIED,
  STATE_DRY_RUN
};

State currentState = STATE_IDLE;

// Objects
WiFiClient espClient;
PubSubClient client(espClient);

// ================= FUNCTION PROTOTYPES =================
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void updateLCD();
void handleLogic();
void requestWater();
void supplyWater(String to, float amount);
void sendResponse(String to, bool accepted, float amount);
void publishTelemetry();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  // Seed Random Generator with noise
  randomSeed(analogRead(0) + micros());

  // Init Hardware
  pinMode(PIN_PUMP_MAIN, OUTPUT);
  pinMode(PIN_PUMP_BACKUP, OUTPUT);
  pinMode(PIN_CURRENT, INPUT);
  pinMode(PIN_LEVEL_INIT, INPUT); // Only for startup

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Node A Init...");

  // Read Initial Level ONCE
  int rawLevel = analogRead(PIN_LEVEL_INIT);
  tankLevel = map(rawLevel, 0, 4095, 0, 100);
  tankLevel = constrain(tankLevel, 0, 100);
  
  Serial.printf("Startup: Initial Tank Level = %.1f%%\n", tankLevel);
  delay(1000);

  // Connect
  setup_wifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
}

// ================= LOOP =================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- MANUAL OVERRIDE CHECK ---
  int currentPotRaw = analogRead(PIN_LEVEL_INIT);
  // Check if pot has moved significantly (> 50 units ~ 1.2%)
  // This allows user to "inject" a new level by moving the pot
  if (abs(currentPotRaw - lastPotRaw) > 50) {
    tankLevel = map(currentPotRaw, 0, 4095, 0, 100);
    tankLevel = constrain(tankLevel, 0, 100);
    lastPotRaw = currentPotRaw;
    Serial.printf("Manual Level Override: %.1f%%\n", tankLevel);
  }
  // -----------------------------

  handleLogic();
  updateLCD();
  publishTelemetry();
  delay(100); // Stability delay
}

// ================= LOGIC & STATE MACHINE =================
void handleLogic() {
  // 1. Dry Run Protection (Hardware priority)
  if (tankLevel < CRITICAL) {
    digitalWrite(PIN_PUMP_MAIN, LOW);
    digitalWrite(PIN_PUMP_BACKUP, LOW);
    
    // Only go to DRY_RUN state if we are not actively fixing it (Requesting/Receiving)
    if (currentState != STATE_REQUESTING && currentState != STATE_RECEIVING) {
        currentState = STATE_DRY_RUN;
    }
  } else {
    // Recovery from Dry Run
    if (currentState == STATE_DRY_RUN) {
      currentState = STATE_IDLE;
    }
  }

  // 2. Request Logic
  // Allow requesting even if we were in DRY_RUN (as long as we aren't supplying/receiving)
  if (currentState != STATE_SUPPLYING && currentState != STATE_RECEIVING) {
    if (tankLevel <= LOW_THRESHOLD) {
      if (!waitingForResponse) {
        if (millis() - lastRequestTime > REQUEST_INTERVAL) {
          requestWater();
          lastRequestTime = millis();
        }
      }
    } else {
      // If we have enough water and were requesting/denied, go back to idle
      if (currentState == STATE_REQUESTING || currentState == STATE_DENIED) {
        currentState = STATE_IDLE;
        waitingForResponse = false;
      }
    }
  }
}

void requestWater() {
  float needed = TARGET_LEVEL - tankLevel;
  if (needed <= 0) return;

  Serial.printf("Requesting %.1f L from %s (Current: %.1f%%)\n", needed, PEER_ID, tankLevel);
  
  StaticJsonDocument<200> doc;
  doc["from"] = NODE_ID;
  doc["to"] = PEER_ID;
  doc["need"] = needed;
  doc["level"] = tankLevel; // Optional: Inform peer of urgency

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(TOPIC_REQUEST, buffer);

  currentState = STATE_REQUESTING;
  waitingForResponse = true;
}

void supplyWater(String requestor, float amount) {
  // Logic: Give water from surplus above 50%
  // This satisfies "till the more water node reach 50%"
  
  float surplus = tankLevel - MIN_KEEP;
  
  if (surplus > 0 && tankLevel > CRITICAL) {
    float actualSupply = (amount < surplus) ? amount : surplus;

    Serial.printf("Supplying %.1f L to %s (Requested: %.1f, Surplus: %.1f)\n", 
                  actualSupply, requestor.c_str(), amount, surplus);
    
    currentState = STATE_SUPPLYING;

    // Pump Sequence
    digitalWrite(PIN_PUMP_MAIN, HIGH);
    
    // Check Current Sensor for Backup Pump
    int currentRaw = analogRead(PIN_CURRENT);
    int currentPct = map(currentRaw, 0, 4095, 0, 100);
    if (currentPct < 20) {
      Serial.println("Main Pump Underload! Activating Backup.");
      lcd.setCursor(0, 1);
      lcd.print("! Backup Pump ! "); // Alert on LCD
      digitalWrite(PIN_PUMP_BACKUP, HIGH);
    } else {
      digitalWrite(PIN_PUMP_BACKUP, LOW);
      // Ensure we display Supplying if main pump is fine
      lcd.setCursor(0, 1);
      lcd.print("Supplying...    ");
    }

    // Simulate pumping duration based on amount (100ms per unit)
    for (int i=0; i<10; i++) {
        client.loop(); 
        delay(actualSupply * 10); 
    }

    // Update Internal Model
    tankLevel -= actualSupply;
    if (tankLevel < 0) tankLevel = 0;

    // Turn off Pumps
    digitalWrite(PIN_PUMP_MAIN, LOW);
    digitalWrite(PIN_PUMP_BACKUP, LOW);

    // Send Success Response with ACTUAL amount supplied
    sendResponse(requestor, true, actualSupply);
    currentState = STATE_IDLE;

  } else {
    Serial.printf("Cannot supply code. Level: %.1f, Keep: %.1f\n", tankLevel, MIN_KEEP);
    sendResponse(requestor, false, 0); // Deny
  }
}

void sendResponse(String to, bool accepted, float amount) {
  StaticJsonDocument<200> doc;
  doc["from"] = NODE_ID;
  doc["to"] = to;
  doc["accepted"] = accepted;
  doc["amount"] = amount;

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(TOPIC_RESPONSE, buffer);
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("Rx [%s]: %s\n", topic, message.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON Parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* toNode = doc["to"];
  const char* fromNode = doc["from"];

  // Filter messages not for us
  if (strcmp(toNode, NODE_ID) != 0) {
    return; 
  }

  // Handle Request
  if (strcmp(topic, TOPIC_REQUEST) == 0) {
    float need = doc["need"];
    supplyWater(String(fromNode), need);
  }
  
  // Handle Response
  else if (strcmp(topic, TOPIC_RESPONSE) == 0) {
    bool accepted = doc["accepted"];
    float amount = doc["amount"];

    if (accepted && waitingForResponse) {
      Serial.printf("Received %.1f L from %s\n", amount, fromNode);
      currentState = STATE_RECEIVING;
      
      // Update Model
      tankLevel += amount;
      if (tankLevel > MAX_LEVEL) tankLevel = MAX_LEVEL;
      
      waitingForResponse = false;
      delay(1000); // Show receiving state briefly
      currentState = STATE_IDLE;
    } 
    else if (!accepted && waitingForResponse) {
      Serial.printf("Request Denied by %s\n", fromNode);
      currentState = STATE_DENIED;
      waitingForResponse = false; 
      // Will retry automatically after interval via handleLogic
    }
  }
}

// ================= HELPERS =================
void updateLCD() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 500) return; // Update every 500ms
  lastUpdate = millis();

  lcd.setCursor(0, 0);
  lcd.printf("ID:%s Lvl:%.0f%%  ", NODE_ID, tankLevel); // Extra spaces to clear

  lcd.setCursor(0, 1);
  switch (currentState) {
    case STATE_IDLE:       lcd.print("Idle            "); break;
    case STATE_REQUESTING: lcd.print("Requesting Water"); break;
    case STATE_SUPPLYING:  lcd.print("Supplying...    "); break;
    case STATE_RECEIVING:  lcd.print("Receiving...    "); break;
    case STATE_DENIED:     lcd.print("Request Denied  "); break;
    case STATE_DRY_RUN:    lcd.print("! DRY RUN !     "); break;
  }
}

void publishTelemetry() {
  if (millis() - lastTelemetry < 2000) return;
  lastTelemetry = millis();

  // Prepare JSON
  StaticJsonDocument<256> doc;
  doc["level_pct"] = tankLevel;
  
  // State String
  switch (currentState) {
    case STATE_IDLE: doc["state"] = "IDLE"; break;
    case STATE_REQUESTING: doc["state"] = "REQUESTING"; break;
    case STATE_SUPPLYING: doc["state"] = "SUPPLYING"; break;
    case STATE_RECEIVING: doc["state"] = "RECEIVING"; break;
    case STATE_DENIED: doc["state"] = "DENIED"; break;
    case STATE_DRY_RUN: doc["state"] = "DRY_RUN_FAULT"; break;
  }

  doc["pump1"] = digitalRead(PIN_PUMP_MAIN) ? "on" : "idle";
  doc["pump2"] = digitalRead(PIN_PUMP_BACKUP) ? "on" : "idle";
  
  // Simulated Current (mA) roughly proportional to pot
  int rawCurrent = analogRead(PIN_CURRENT);
  doc["pump1_current_mA"] = map(rawCurrent, 0, 4095, 0, 500);
  doc["uptime_s"] = millis() / 1000;
  doc["fault"] = (currentState == STATE_DRY_RUN);

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(TOPIC_STATUS, buffer);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a VERY unique client ID to avoid collisions
    String clientId = "WG_2026_NodeA_";
    clientId += String(random(10000, 99999)); // Longer random suffix
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(TOPIC_REQUEST);
      client.subscribe(TOPIC_RESPONSE);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
