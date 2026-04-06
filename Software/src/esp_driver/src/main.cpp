// AI Nutritious Culinary Assistant — ESP32-S3 Dispenser Firmware (BLE)
// TB6612 motor driver, FULL4WIRE steppers, BLE communication.
// No HX711 load cell for now (weight always reports 0).
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
//   Motor Right  (container 0) → AIN1=10, AIN2=11, BIN1=12, BIN2=13
//   Motor Middle (container 1) → TODO: assign unique pins when wired
//   Motor Left   (container 2) → TODO: assign unique pins when wired
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

// ── BLE UUIDs ─────────────────────────────────────────────────────────────────

#define SERVICE_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a0"
#define CMD_CHAR_UUID  "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a1" // Write
#define RSLT_CHAR_UUID "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a2" // Notify
#define WT_CHAR_UUID   "4e7a9b1c-d203-4e2a-b8f1-67c1d9e3f5a3" // Notify

const char *BLE_DEVICE_NAME = "NuChef-Dispenser";

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
// container_index from backend maps directly to motors[index]
//
// Right motor (container 0) — the only one wired for now
#define AIN1R 10
#define AIN2R 11
#define BIN1R 12
#define BIN2R 13

// Middle motor (container 1) — TODO: update when wired
#define AIN1M 10   // placeholder — change to real pins
#define AIN2M 11
#define BIN1M 12
#define BIN2M 13

// Left motor (container 2) — TODO: update when wired
#define AIN1L 10   // placeholder — change to real pins
#define AIN2L 11
#define BIN1L 12
#define BIN2L 13

AccelStepper motors[3] = {
    AccelStepper(AccelStepper::FULL4WIRE, AIN1R, AIN2R, BIN1R, BIN2R),
    AccelStepper(AccelStepper::FULL4WIRE, AIN1M, AIN2M, BIN1M, BIN2M),
    AccelStepper(AccelStepper::FULL4WIRE, AIN1L, AIN2L, BIN1L, BIN2L),
};

const float MOTOR_MAX_SPEED    = 300.0f;
const float MOTOR_ACCELERATION = 100.0f;
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
  long stepsTarget = (long)(targetGrams * STEPS_PER_GRAM);
  bool timedOut = false;

  motor.setCurrentPosition(0);
  motor.moveTo(stepsTarget);

  unsigned long startMs = millis();

  while (motor.distanceToGo() != 0)
  {
    motor.run();

    if (millis() - startMs > DISPENSE_TIMEOUT_MS)
    {
      motor.stop();
      timedOut = true;
      Serial.println("Dispense timed out.");
      break;
    }
  }

  // Without a load cell we report the target as actual
  float actualGrams = timedOut ? 0.0f : targetGrams;
  const char *status = timedOut ? "error" : "done";

  Serial.printf("Dispense %s — actual %.2fg\n", status, actualGrams);

  notifyResult(commandId, status, actualGrams, 0.0f);
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

  for (AccelStepper &m : motors)
  {
    m.setMaxSpeed(MOTOR_MAX_SPEED);
    m.setAcceleration(MOTOR_ACCELERATION);
  }
  Serial.println("Motors configured.");

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
    notifyWeight(0.0f); // no load cell — always 0
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

  // Keep all motors ticking (needed for FULL4WIRE AccelStepper)
  for (AccelStepper &m : motors)
    m.run();
}