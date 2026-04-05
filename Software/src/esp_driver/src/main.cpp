// AI Nutritious Culinary Assistant — ESP32 Dispenser Firmware
// TB6600 stepper drivers (step/dir), HX711 load cell, FastAPI backend polling.
//
// Wiring summary:
//   Motor 0 (salt)         → STEP=25, DIR=26
//   Motor 1 (black pepper) → STEP=27, DIR=14
//   Motor 2 (garlic powder)→ STEP=16, DIR=17
//   HX711                  → DT=34,   SCK=35
//
// Calibration:
//   GRAMS_SCALE_FACTOR — raw HX711 units per gram (run tare + known weight)
//   STEPS_PER_GRAM     — motor steps per gram dispensed (measure empirically)

#include <Arduino.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "HX711.h"

// ── WiFi ──────────────────────────────────────────────────────────────────────

const char *SSID = "Triangle";
const char *PASSWORD = "houserules";

// ── Backend ───────────────────────────────────────────────────────────────────
// UPDATE this to the machine running backend.py (uvicorn --host 0.0.0.0)

// const char *BACKEND = "http://192.168.4.28:8000";    // 508 E John
const char *BACKEND = "http://192.168.197.86:8000";   // Triangle

const unsigned long POLL_MS = 1000;      // ms between /pending_command polls
const unsigned long HEARTBEAT_MS = 2000; // ms between /status_update posts

// ── HX711 load cell ───────────────────────────────────────────────────────────

#define HX711_DT 34
#define HX711_SCK 35

HX711 scale;
const float GRAMS_SCALE_FACTOR = 420.0f; // CALIBRATE: raw_units / gram

// ── Motors — TB6600 in DRIVER (step/dir) mode ─────────────────────────────────
// container_index from backend maps directly to motors[index]

#define MOTOR0_STEP 25
#define MOTOR0_DIR 26
#define MOTOR1_STEP 27
#define MOTOR1_DIR 14
#define MOTOR2_STEP 16
#define MOTOR2_DIR 17

AccelStepper motors[3] = {
    AccelStepper(AccelStepper::DRIVER, MOTOR0_STEP, MOTOR0_DIR),
    AccelStepper(AccelStepper::DRIVER, MOTOR1_STEP, MOTOR1_DIR),
    AccelStepper(AccelStepper::DRIVER, MOTOR2_STEP, MOTOR2_DIR),
};

const float MOTOR_MAX_SPEED = 800.0f;
const float MOTOR_ACCELERATION = 400.0f;
const float STEPS_PER_GRAM = 320.0f;             // CALIBRATE: steps / gram
const float GRAM_TOLERANCE = 0.2f;               // stop within ±0.2 g of target
const unsigned long DISPENSE_TIMEOUT_MS = 15000; // 15 s hard timeout

// ── Timing ────────────────────────────────────────────────────────────────────

unsigned long lastPoll = 0;
unsigned long lastHeartbeat = 0;
bool dispensing = false;

// ── WiFi ──────────────────────────────────────────────────────────────────────

void connectWiFi()
{
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ── Load cell ─────────────────────────────────────────────────────────────────

float readWeight()
{
  if (!scale.is_ready())
    return 0.0f;
  return scale.get_units(3); // average 3 readings for stability
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

void postCommandResult(const String &commandId, const char *status,
                       float actualGrams, float weight)
{
  HTTPClient http;
  http.begin(String(BACKEND) + "/command_result");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["command_id"] = commandId;
  doc["status"] = status;
  doc["actual_grams"] = actualGrams;
  doc["weight"] = weight;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  Serial.printf("/command_result → HTTP %d\n", code);
  http.end();
}

void postStatusUpdate(float weight)
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(String(BACKEND) + "/status_update");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["weight"] = weight;

  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
}

// Polls /pending_command. Returns true and fills out params if a dispense
// command is waiting; returns false if nothing to do.
bool pollCommand(String &outId, int &outContainerIdx, float &outTargetGrams)
{
  if (WiFi.status() != WL_CONNECTED)
    return false;

  HTTPClient http;
  http.begin(String(BACKEND) + "/pending_command");
  int code = http.GET();
  if (code != 200)
  {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload))
    return false;

  const char *action = doc["action"] | "none";
  if (strcmp(action, "dispense") != 0)
    return false;

  const char *cmdId = doc["command_id"] | "";
  if (strlen(cmdId) == 0)
    return false;

  outId = String(cmdId);
  outContainerIdx = doc["container_index"] | -1;
  outTargetGrams = doc["target_grams"] | 0.0f;

  if (outContainerIdx < 0 || outContainerIdx > 2 || outTargetGrams <= 0.0f)
    return false;

  return true;
}

// ── Dispense ──────────────────────────────────────────────────────────────────

void runDispense(int containerIdx, float targetGrams, const String &commandId)
{
  // ── SINGLE-MOTOR TEST MODE ────────────────────────────────────────────────
  // Set TEST_MOTOR_INDEX to 0, 1, or 2 to always use that motor regardless
  // of the container_index sent by the backend.
  // Set to -1 to disable test mode and use the real container_index.
  const int TEST_MOTOR_INDEX = 0;
  if (TEST_MOTOR_INDEX >= 0)
    containerIdx = TEST_MOTOR_INDEX;
  // ─────────────────────────────────────────────────────────────────────────

  Serial.printf("Dispensing %.2fg from container %d\n", targetGrams, containerIdx);

  AccelStepper &motor = motors[containerIdx];
  float baseline = readWeight();
  long stepsTarget = (long)(targetGrams * STEPS_PER_GRAM);
  bool timedOut = false;

  motor.setCurrentPosition(0);
  motor.moveTo(stepsTarget);

  unsigned long startMs = millis();

  while (motor.distanceToGo() != 0)
  {
    motor.run();

    float dispensed = readWeight() - baseline;
    if (dispensed >= targetGrams - GRAM_TOLERANCE)
    {
      motor.stop();
      break;
    }

    if (millis() - startMs > DISPENSE_TIMEOUT_MS)
    {
      motor.stop();
      timedOut = true;
      Serial.println("Dispense timed out.");
      break;
    }
  }

  delay(300); // let scale settle
  float finalWeight = readWeight();
  float actualGrams = finalWeight - baseline;
  const char *status = timedOut ? "error" : "done";

  Serial.printf("Dispense %s — actual %.2fg, scale %.2fg\n",
                status, actualGrams, finalWeight);

  postCommandResult(commandId, status, actualGrams, finalWeight);
}

// ── Setup & loop ──────────────────────────────────────────────────────────────

void setup()
{
  Serial.begin(115200);

  for (AccelStepper &m : motors)
  {
    m.setMaxSpeed(MOTOR_MAX_SPEED);
    m.setAcceleration(MOTOR_ACCELERATION);
  }

  scale.begin(HX711_DT, HX711_SCK);
  scale.set_scale(GRAMS_SCALE_FACTOR);
  scale.tare();
  Serial.println("HX711 tared and ready.");

  connectWiFi();
  Serial.printf("Backend: %s\n", BACKEND);
}

void loop()
{
  unsigned long now = millis();

  // Periodic weight heartbeat to backend
  if (now - lastHeartbeat >= HEARTBEAT_MS)
  {
    lastHeartbeat = now;
    postStatusUpdate(readWeight());
  }

  // Poll for dispense commands (skip while a dispense is running)
  if (!dispensing && (now - lastPoll >= POLL_MS))
  {
    lastPoll = now;

    String cmdId;
    int containerIdx;
    float targetGrams;

    if (pollCommand(cmdId, containerIdx, targetGrams))
    {
      dispensing = true;
      runDispense(containerIdx, targetGrams, cmdId);
      dispensing = false;
    }
  }
}