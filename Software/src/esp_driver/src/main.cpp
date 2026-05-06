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
#include <Wire.h>
#include "Adafruit_VL53L0X.h"

// ── BLE UUIDs ─────────────────────────────────────────────────────────────────

#define SERVICE_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a0"
#define CMD_CHAR_UUID  "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a1" // Write
#define RSLT_CHAR_UUID "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a2" // Notify
#define WT_CHAR_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a3" // Notify

const char *BLE_DEVICE_NAME = "NuChef-Dispenser";

// ── Load cell (HX711) ──────────────────────────────────────────────────────────

const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN  = 1;
const float HX711_SCALE_FACTOR = float(1250.0f);  // (raw reading) / actual grams — calibrated 2026-04-29
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

// Calibration step command
volatile bool calibStepReady = false;
String calibStepCmdId;
int    calibMotorIdx = -1;
long   calibSteps    = 0;

// Reset-position command
volatile bool calibResetReady = false;
String calibResetCmdId;

// ── Timing ────────────────────────────────────────────────────────────────────

const unsigned long HEARTBEAT_MS    = 2000;  // ms between weight notifications
const unsigned long IR_HEARTBEAT_MS = 5000;  // ms between IR sensor sweeps
unsigned long lastHeartbeat   = 0;
unsigned long lastIrRead      = 0;
bool dispensing = false;

// ── IR distance sensors (VL53L0X × 3, one shared object, time-multiplexed I2C) ─
// Left  → chili pepper  (SDA=35, SCL=36, empty=90mm, full=45mm)
// Middle → MSG          (SDA=38, SCL=37, empty=111mm, full=80mm)
// Right  → salt         (SDA=40, SCL=39, empty=90mm,  full=32mm)

struct IrSensorConfig {
  const char* name;
  int         sda;
  int         scl;
  float       emptyMm;  // distance when container is empty
  float       fullMm;   // distance when container is full
};

const IrSensorConfig IR_SENSORS[3] = {
  {"left",   35, 36, 145.0f, 48.0f},   // chili pepper
  {"middle", 38, 37, 134.0f, 42.0f},  // MSG (calibrated: 109.2mm→80%)
  {"right",  40, 39,  144.0f,  45.0f},  // salt (calibrated: 66.2mm→85%)
};

Adafruit_VL53L0X irLox;   // single shared object — re-init each sensor
int irLevels[3] = {-1, -1, -1};  // fill % (0-100), -1 = not yet read

// Read one VL53L0X by switching the I2C bus to its pins.
// Returns fill percentage 0-100, or -1 on failure.
int readIrSensor(const IrSensorConfig& s)
{
  Wire.end();
  delay(5);
  Wire.begin(s.sda, s.scl);
  Wire.setClock(100000);
  delay(5);

  if (!irLox.begin(0x29, false, &Wire)) {
    Serial.printf("IR[%s]: VL53L0X init failed\n", s.name);
    return -1;
  }

  const int SAMPLES   = 12;
  const int MIN_VALID = 4;  // need at least this many good samples
  float sum   = 0.0f;
  int   valid = 0;

  for (int i = 0; i < SAMPLES; i++) {
    VL53L0X_RangingMeasurementData_t m;
    irLox.rangingTest(&m, false);
    if (m.RangeStatus != 4) {   // 4 = out-of-range
      sum += m.RangeMilliMeter;
      valid++;
    }
    delay(5);
  }

  if (valid < MIN_VALID) {
    Serial.printf("IR[%s]: too many OOR readings\n", s.name);
    return -1;
  }

  float reading = sum / valid;
  float range   = s.fullMm - s.emptyMm;
  float pct     = (range != 0.0f) ? (reading - s.emptyMm) / range : 0.0f;
  // Clamp and snap near edges
  if (pct <= 0.05f) pct = 0.0f;
  if (pct >= 0.95f) pct = 1.0f;
  int level = (int)(pct * 100.0f + 0.5f);
  level = max(0, min(100, level));

  Serial.printf("IR[%s]: %.1fmm → %d%%\n", s.name, reading, level);
  return level;
}

void readAllIrSensors()
{
  for (int i = 0; i < 3; i++) {
    int lvl = readIrSensor(IR_SENSORS[i]);
    if (lvl >= 0) irLevels[i] = lvl;  // keep previous value on failure
  }

  // Print all spice levels together in backend container order.
  // container 0 = salt (right sensor), 1 = msg (middle), 2 = chili pepper (left)
  Serial.printf("Spice levels -> salt:%d%%  msg:%d%%  chili:%d%%\n",
                irLevels[2], irLevels[1], irLevels[0]);
}

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

// ── Per-motor calibration: measured grams dispensed per one rotation cycle ────
const float GRAMS_PER_ROTATION[3] = {1.1f, 0.7f, 0.27f};  // salt, msg, chili pepper

const float MOTOR_MAX_SPEED    = 800.0f;  // from motor_test.cpp
const float MOTOR_ACCELERATION = 200.0f;  // from motor_test.cpp
const float STEPS_PER_GRAM     = 320.0f;          // CALIBRATE: steps / gram
const unsigned long DISPENSE_TIMEOUT_MS = 30000;   // 30 s hard timeout (covers multi-cycle)

// ── Per-motor two-part cycle calibration ─────────────────────────────────────
// Motors are manually calibrated to their zero (closed) position first.
// Each dispense cycle rotates 180° forward then returns to 0°.
// 200 full steps = 360° → 100 steps = 180°
const long STEPS_180 = 100;   // forward leg: 0° → 180°
                               // back leg:    180° → 0°  (same count, reverse dir)
const unsigned long CYCLE_PAUSE_MS = 500;  // pause between forward and back leg


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

    // ── motor_step — calibration nudge ──
    if (strcmp(action, "motor_step") == 0) {
      int  mIdx  = doc["motor"] | -1;
      long steps = doc["steps"] | 0;
      const char *id = doc["command_id"] | "";
      if (mIdx < 0 || mIdx > 2 || steps == 0) return;
      calibStepCmdId = String(id);
      calibMotorIdx  = mIdx;
      calibSteps     = steps;
      calibStepReady = true;
      return;
    }

    // ── motor_reset_position — zero all position counters ──
    if (strcmp(action, "motor_reset_position") == 0) {
      calibResetCmdId  = String(doc["command_id"] | "reset");
      calibResetReady  = true;
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

  // Include IR fill levels when available so ble_bridge forwards them
  // to /status_update → container_levels in the web dashboard.
  // Backend CONTAINERS = ["salt", "msg", "chili pepper"] (indices 0,1,2).
  // Physical IR mapping: left(0)=chili pepper, middle(1)=MSG, right(2)=salt.
  // So reorder: container_levels[0]=salt=irLevels[2],
  //             container_levels[1]=MSG=irLevels[1],
  //             container_levels[2]=chili=irLevels[0].
  if (irLevels[0] >= 0 || irLevels[1] >= 0 || irLevels[2] >= 0) {
    auto arr = doc["container_levels"].to<JsonArray>();
    arr.add(irLevels[2] >= 0 ? irLevels[2] : 100);  // salt (right sensor)
    arr.add(irLevels[1] >= 0 ? irLevels[1] : 100);  // MSG  (middle sensor)
    arr.add(irLevels[0] >= 0 ? irLevels[0] : 100);  // chili pepper (left sensor)
  }

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

  // Compute how many full rotation cycles cover targetGrams, min 1.
  float gPerRot  = GRAMS_PER_ROTATION[containerIdx];
  int numCycles  = max(1, (int)roundf(targetGrams / gPerRot));
  Serial.printf("Cycle: 0→%ld→0  grams/rot=%.2f  → %d cycle(s) for %.2fg\n",
                STEPS_180, gPerRot, numCycles, targetGrams);

  float weightBefore = readWeightBlocking(500);
  bool timedOut = false;
  unsigned long startMs = millis();

  // Home position is 0 — motor was calibrated here before use.
  dispenseMotor.setCurrentPosition(0);

  for (int cycle = 0; cycle < numCycles && !timedOut; cycle++)
  {
    // ── Leg 1: move to 180° (open) ────────────────────────────────────────
    dispenseMotor.moveTo(STEPS_180);
    while (dispenseMotor.distanceToGo() != 0)
    {
      dispenseMotor.run();
      if (millis() - startMs > DISPENSE_TIMEOUT_MS)
      {
        dispenseMotor.stop();
        timedOut = true;
        Serial.println("Dispense timed out (open leg).");
        break;
      }
    }

    if (timedOut) break;

    // ── Pause at open position ─────────────────────────────────────────────
    delay(CYCLE_PAUSE_MS);

    // ── Leg 2: return to 0° (home/closed) ─────────────────────────────────
    dispenseMotor.moveTo(0);
    while (dispenseMotor.distanceToGo() != 0)
    {
      dispenseMotor.run();
      if (millis() - startMs > DISPENSE_TIMEOUT_MS)
      {
        dispenseMotor.stop();
        timedOut = true;
        Serial.println("Dispense timed out (close leg).");
        break;
      }
    }

    Serial.printf("  cycle %d/%d done\n", cycle + 1, numCycles);
  }

  disableAllMotors();

  // Read final weight from load cell (blocking — motor stopped)
  float weightAfter = readWeightBlocking(500);
  float actualGrams = timedOut ? 0.0f : max(0.0f, weightAfter - weightBefore);
  if (!scaleReady) actualGrams = timedOut ? 0.0f : targetGrams;
  const char *status = timedOut ? "error" : "done";

  Serial.printf("Dispense %s — actual %.2fg, weight %.2fg\n",
                status, actualGrams, weightAfter);

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

  // Periodic IR sensor sweep (skipped while dispensing to avoid I2C bus conflicts)
  if (!dispensing && (now - lastIrRead >= IR_HEARTBEAT_MS))
  {
    lastIrRead = now;
    readAllIrSensors();
    
  }

  // Periodic weight heartbeat via BLE notify (includes latest IR levels)
  if (now - lastHeartbeat >= HEARTBEAT_MS)
  {
    lastHeartbeat = now;
    float w = readWeight() * 2.938f;  // convert back to grams for the backend (calibrated: 1g=2.937 raw)
    notifyWeight(w);
  }

  // ── Process calibration step command ─────────────────────────────────────
  if (!dispensing && calibStepReady)
  {
    calibStepReady = false;
    int  mIdx  = calibMotorIdx;
    long steps = calibSteps;
    Serial.printf(">>> Calib step: motor=%d, steps=%ld\n", mIdx, steps);
    selectMotor(mIdx);
    dispenseMotor.setCurrentPosition(0);
    dispenseMotor.moveTo(steps);
    while (dispenseMotor.distanceToGo() != 0)
      dispenseMotor.run();
    while (dispenseMotor.isRunning())
      dispenseMotor.run();
    disableAllMotors();
    Serial.println(">>> Calib step done.");
    // Send result
    if (deviceConnected) {
      notifyResult(calibStepCmdId, "done", 0.0f, currentWeight);
    }
  }

  // ── Process reset-position command ────────────────────────────────────────
  if (!dispensing && calibResetReady)
  {
    calibResetReady = false;
    dispenseMotor.setCurrentPosition(0);
    Serial.println(">>> Motor positions reset to zero.");
    if (deviceConnected) {
      notifyResult(calibResetCmdId, "done", 0.0f, currentWeight);
    }
  }

  // ── Process pending dispense command (skip while a dispense is running) ───
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