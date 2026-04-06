// BLE motor control - send "spin" or "stop" from a BLE terminal app

#include <Arduino.h>
#include <AccelStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define AIN1R 10
#define AIN2R 11
#define BIN1R 12
#define BIN2R 13

#define AIN1M 10
#define AIN2M 11
#define BIN1M 12
#define BIN2M 13

AccelStepper stepperRight(
  AccelStepper::FULL4WIRE,
  AIN1R, AIN2R, BIN1R, BIN2R
);

AccelStepper stepperMiddle(
    AccelStepper::FULL4WIRE,
    AIN1M, AIN2M, BIN1M, BIN2M
);

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

bool motorRunning = false;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("BLE client connected");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("BLE client disconnected");
        pServer->startAdvertising();
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue().c_str();
        value.trim();
        value.toLowerCase();
        Serial.print("Received: ");
        Serial.println(value);

        if (value == "spin") {
            motorRunning = true;
            stepperRight.setCurrentPosition(0);
            stepperRight.moveTo(50);
            stepperMiddle.setCurrentPosition(0);
            stepperMiddle.moveTo(50);
            Serial.println("Motors spinning!");
        } else if (value == "stop") {
            motorRunning = false;
            stepperRight.stop();
            stepperMiddle.stop();
            Serial.println("Motors stopped!");
        }
    }
};

void setup()
{
    Serial.begin(115200);
    Serial.println("BLE Motor Control starting...");

    stepperRight.setMaxSpeed(300);
    stepperRight.setAcceleration(100);
    stepperMiddle.setMaxSpeed(300);
    stepperMiddle.setAcceleration(100);

    // Initialize BLE
    BLEDevice::init("NuChef-Motor");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE
    );

    pCharacteristic->setCallbacks(new CommandCallbacks());
    pCharacteristic->setValue("ready");
    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.println("BLE ready! Device name: NuChef-Motor");
    Serial.println("Send 'spin' or 'stop' from a BLE terminal app");
}

void loop()
{
    stepperRight.run();
    stepperMiddle.run();

    if (motorRunning && !stepperRight.isRunning() && !stepperMiddle.isRunning()) {
        stepperRight.moveTo(-stepperRight.currentPosition());
        stepperMiddle.moveTo(-stepperMiddle.currentPosition());
    }
}