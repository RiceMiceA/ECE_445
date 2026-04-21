// AI Nutritious Culinary Assistant — ESP32-S3 Dispenser Firmware (BLE)
// TB6612 motor driver, FULL4WIRE steppers, BLE communication.
// HX711 load cell on pins DOUT=2, SCK=1 for real-time weight.
//
// BLE Protocol:
//   Advertises as "NuChef-Dispenser" with a custom GATT service.
//   A BLE central (ble_bridge.py on the backend machine) connects and
//   communicates via three characteristics:
//
//     CMD_CHAR  (write):  receives dispense commands
//       → {"action":"dispense","command_id":"...","container_index":0,"target_grams":2.5}
//     RSLT_CHAR (notify): sends command results
//       → {"command_id":"...","status":"done","actual_grams":2.5,"weight":0}
//     WT_CHAR   (notify): sends periodic weight heartbeats
//       → {"weight":0}
//
// Wiring summary  (TB6612 → ESP32-S3):
//   All three motors share the 4 coil-drive pins below.
//   The active motor is selected by its ENABLE line.
//   container_index 0 → Left motor
//   container_index 1 → Middle motor
//   container_index 2 → Right motor
//
// Calibration:
//   STEPS_PER_GRAM — motor steps per gram dispensed (measure empirically)

#include <Arduino.h>
#include <AccelStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include "HX711.h"

// ── BLE UUIDs ─────────────────────────────────────────────────────────────────

#define SERVICE_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a0"
#define CMD_CHAR_UUID  "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a1" // Write
#define RSLT_CHAR_UUID "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a2" // Notify
#define WT_CHAR_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a3" // Notify

const char *BLE_DEVICE_NAME = "NuChef-Dispenser";

// ── Load cell (HX711) ──────────────────────────────────────────────────────────

const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN  = 1;
const float HX711_SCALE_FACTOR = 1250.0f;  // (raw reading) / actual grams — calibrate!
const float WEIGHT_ZERO_THRESHOLD = 0.05f; // readings below this → 0
const unsigned long HX711_READY_TIMEOUT_MS = 3000;

HX711 scale;
bool  scaleReady = false;   // set true only if tare succeeds
float currentWeight = 0.0f; // latest reading in grams

// ── BLE state ─────────────────────────────────────────────────────────────────

BLEServer         *pServer   = nullptr;
BLECharacteristic *pCmdChar  = nullptr;
BLECharacteristic *pRsltChar = nullptr;
BLECharacteristic *pWtChar   = nullptr;

bool deviceConnected    = false;
bool oldDeviceConnected = false;

// Command buffer — written by the BLE callback, consumed in loop()
volatile bool commandReady = false;
String  pendingCmdId;
int     pendingContainerIdx = -1;
float   pendingTargetGrams  = 0.0f;

// ── Timing ────────────────────────────────────────────────────────────────────

const unsigned long HEARTBEAT_MS = 2000; // ms between weight notifications
unsigned long lastHeartbeat = 0;
bool dispensing = false;

// ── Motors — TB6612 FULL4WIRE steppers ────────────────────────────────────────
// All three spice augers share the same 4 phase lines.
// We select Left / Middle / Right with the dedicated enable pins.

#define AIN1R 21
#define AIN2R 47
#define BIN1R 45
#define BIN2R 48
#define ENABLE_R 14

#define ENABLE_M 13
#define ENABLE_L 12

enum MotorSlot : int
{
  MOTOR_LEFT = 0,
  MOTOR_MIDDLE = 1,
  MOTOR_RIGHT = 2,
};

const int MOTOR_ENABLE_PINS[3] = {ENABLE_L, ENABLE_M, ENABLE_R};
const char *MOTOR_SLOT_NAMES[3] = {"left", "middle", "right"};

AccelStepper dispenseMotor(AccelStepper::FULL4WIRE, AIN1R, AIN2R, BIN1R, BIN2R);

const float MOTOR_MAX_SPEED    = 800.0f;  // from motor_test.cpp
const float MOTOR_ACCELERATION = 200.0f;  // from motor_test.cpp
const float STEPS_PER_GRAM     = 320.0f;          // CALIBRATE: steps / gram
const unsigned long DISPENSE_TIMEOUT_MS = 15000;   // 15 s hard timeout


// ── BLE Callbacks ─────────────────────────────────────────────────────────────

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *s) override
  {
    deviceConnected = true;
    Serial.println("BLE central connected.");
  }
  void onDisconnect(BLEServer *s) override
  {
    deviceConnected = false;
    Serial.println("BLE central disconnected.");
  }
};

// Called when the central writes a JSON command to CMD_CHAR
class CmdCharCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar) override
  {
    std::string raw = pChar->getValue();
    if (raw.empty()) return;

    Serial.printf("BLE CMD rx: %s\n", raw.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, raw.c_str()))
    {
      Serial.println("  → JSON parse error, ignoring.");
      return;
    }

    const char *action = doc["action"] | "none";

    // ── Tare command — immediate, no queuing ──
    if (strcmp(action, "tare") == 0) {
      if (scaleReady) {
        scale.tare();
        currentWeight = 0.0f;
        Serial.println("HX711: tared via BLE.");
        // Send result back
        if (deviceConnected) {
          JsonDocument rdoc;
          rdoc["command_id"] = doc["command_id"] | "tare";
          rdoc["status"]     = "done";
          rdoc["actual_grams"] = 0;
          rdoc["weight"]     = 0.0f;
          String body;
          serializeJson(rdoc, body);
          pRsltChar->setValue(body.c_str());
          pRsltChar->notify();
        }
      } else {
        Serial.println("HX711: tare requested but scale not ready.");
      }
      return;
    }

    if (strcmp(action, "dispense") != 0) return;

    const char *id = doc["command_id"] | "";
    if (strlen(id) == 0) return;

    int   idx   = doc["container_index"] | -1;
    float grams = doc["target_grams"]    | 0.0f;
    if (idx < 0 || idx > 2 || grams <= 0.0f) return;

    // Store for loop() to pick up
    pendingCmdId        = String(id);
    pendingContainerIdx = idx;
    pendingTargetGrams  = grams;
    commandReady        = true;
  }
};

// ── BLE notify helpers ────────────────────────────────────────────────────────

void notifyResult(const String &commandId, const char *status,
                  float actualGrams, float weight)
{
  if (!deviceConnected) return;

  JsonDocument doc;
  doc["command_id"]   = commandId;
  doc["status"]       = status;
  doc["actual_grams"] = actualGrams;
  doc["weight"]       = weight;

  String body;
  serializeJson(doc, body);
  pRsltChar->setValue(body.c_str());
  pRsltChar->notify();
  Serial.printf("BLE RSLT tx: %s\n", body.c_str());
}

void notifyWeight(float weight)
{
  if (!deviceConnected) return;

  JsonDocument doc;
  doc["weight"] = weight;

  String body;
  serializeJson(doc, body);
  pWtChar->setValue(body.c_str());
  pWtChar->notify();
}

// ── Load cell helpers ─────────────────────────────────────────────────────────

void disableAllMotors()
{
  for (int pin : MOTOR_ENABLE_PINS)
    digitalWrite(pin, LOW);
}

bool selectMotor(int containerIdx)
{
  if (containerIdx < 0 || containerIdx > 2) return false;

  disableAllMotors();
  digitalWrite(MOTOR_ENABLE_PINS[containerIdx], HIGH); // held HIGH for entire dispense
  Serial.printf("Selected %s motor (container %d) — ENABLE HIGH.\n",
                MOTOR_SLOT_NAMES[containerIdx], containerIdx);
  return true;
}

// Non-blocking read — safe to call inside the stepper loop.
// Returns the cached value if the HX711 isn't ready yet.
float readWeight()
{
  if (!scaleReady || !scale.is_ready()) return currentWeight;

  float reading = scale.get_units(3);
  if (fabs(reading) <= WEIGHT_ZERO_THRESHOLD) reading = 0.0f;
  currentWeight = reading;
  return currentWeight;
}

// Blocking read — up to `timeoutMs` ms. Use only when stepping is paused.
float readWeightBlocking(unsigned long timeoutMs = 500)
{
  if (!scaleReady) return currentWeight;
  if (!scale.wait_ready_timeout(timeoutMs, 5)) return currentWeight;
  return readWeight();
}

// ── Dispense ──────────────────────────────────────────────────────────────────

void runDispense(int containerIdx, float targetGrams, const String &commandId)
{
  if (!selectMotor(containerIdx))
  {
    Serial.printf("Invalid container index %d\n", containerIdx);
    notifyResult(commandId, "error", 0.0f, readWeight());
    return;
  }

  Serial.printf("Dispensing %.2fg from %s motor (container %d)\n",
                targetGrams, MOTOR_SLOT_NAMES[containerIdx], containerIdx);

  float weightBefore = readWeightBlocking(500);
  long stepsTarget = (long)(targetGrams * STEPS_PER_GRAM);
  bool timedOut = false;

  dispenseMotor.setCurrentPosition(0);
  dispenseMotor.moveTo(stepsTarget);

  Serial.printf("Steps target: %ld, distanceToGo: %ld, maxSpeed: %.1f\n",
                stepsTarget, dispenseMotor.distanceToGo(), dispenseMotor.maxSpeed());

  unsigned long startMs = millis();

  // Keep stepping continuously (same style as motor_test.cpp).
  // For now, do not stop early by weight; stop only by step target or timeout.

  // TONY CHANGE THIS LATER TO DO A SIMPLE PID CONTROL USING THE LOAD CELL AS INPUT.
  while (dispenseMotor.distanceToGo() != 0)
  {
    dispenseMotor.run();

    if (millis() - startMs > DISPENSE_TIMEOUT_MS)
    {
      dispenseMotor.stop();
      timedOut = true;
      Serial.println("Dispense timed out.");
      break;
    }
  }

  while (dispenseMotor.isRunning())
  {
    dispenseMotor.run();
  }

  disableAllMotors();

  // Read final weight from load cell (blocking — motor stopped)
  float weightAfter = readWeightBlocking(500);
  float actualGrams = timedOut ? 0.0f : max(0.0f, weightAfter - weightBefore);
  if (!scaleReady) actualGrams = timedOut ? 0.0f : targetGrams;
  const char *status = timedOut ? "error" : "done";

  Serial.printf("Dispense %s — actual %.2fg, weight %.2fg\n",
                status,
                actualGrams,
                weightAfter);

  notifyResult(commandId, status, actualGrams, weightAfter);
}

// ── BLE initialisation ───────────────────────────────────────────────────────

void initBLE()
{
  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // CMD — central writes dispense commands here
  pCmdChar = pService->createCharacteristic(
      CMD_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE);
  pCmdChar->setCallbacks(new CmdCharCallbacks());
  pCmdChar->setValue("ready");

  // RSLT — ESP32 notifies with command results
  pRsltChar = pService->createCharacteristic(
      RSLT_CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pRsltChar->addDescriptor(new BLE2902());

  // WT — ESP32 notifies with periodic weight
  pWtChar = pService->createCharacteristic(
      WT_CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pWtChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->start();

  Serial.printf("BLE advertising as \"%s\"\n", BLE_DEVICE_NAME);
}

// ── Setup & loop ──────────────────────────────────────────────────────────────

void setup()
{
  Serial.begin(115200);
  delay(1500); // give USB CDC time to enumerate
  Serial.println("\n=== NuChef Dispenser (BLE) ===");

  pinMode(ENABLE_R, OUTPUT);
  pinMode(ENABLE_M, OUTPUT);
  pinMode(ENABLE_L, OUTPUT);
  disableAllMotors();

  dispenseMotor.setMaxSpeed(MOTOR_MAX_SPEED);
  dispenseMotor.setAcceleration(MOTOR_ACCELERATION);
  Serial.println("Motors configured.");

  // ── HX711 load cell init ───────────────────────────────────────────────────
  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  if (scale.wait_ready_timeout(HX711_READY_TIMEOUT_MS, 10))
  {
    scale.set_scale(HX711_SCALE_FACTOR);
    scale.tare(10);
    scaleReady = true;
    currentWeight = 0.0f;
    Serial.println("HX711: ready and tared.");
  }
  else
  {
    scaleReady = false;
    Serial.println("HX711: not ready at boot — weight feedback disabled.");
  }

  initBLE();
}

void loop()
{
  unsigned long now = millis();

  // Handle BLE reconnection (restart advertising after disconnect)
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500); // give the BLE stack time to clean up
    BLEDevice::startAdvertising();
    Serial.println("Restarted BLE advertising.");
    oldDeviceConnected = false;
  }
  if (deviceConnected && !oldDeviceConnected)
  {
    oldDeviceConnected = true;
  }

  // Periodic weight heartbeat via BLE notify
  if (now - lastHeartbeat >= HEARTBEAT_MS)
  {
    lastHeartbeat = now;
    float w = readWeight();
    notifyWeight(w);
  }

  // Process pending dispense command (skip while a dispense is running)
  if (!dispensing && commandReady)
  {
    commandReady = false;
    dispensing   = true;
    Serial.printf(">>> Starting dispense: container=%d, grams=%.2f, id=%s\n",
                  pendingContainerIdx, pendingTargetGrams, pendingCmdId.c_str());
    runDispense(pendingContainerIdx, pendingTargetGrams, pendingCmdId);
    Serial.println(">>> Dispense finished.");
    dispensing   = false;
  }

  // Keep the shared stepper bus ticking if a stop is still decelerating.
  dispenseMotor.run();
}