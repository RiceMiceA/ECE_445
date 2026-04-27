// Simple stepper motor test — no BLE, no load cell
// Spins the right motor (container 0) back and forth continuously.
// Upload with: build_src_filter = +<motor_test.cpp> in platformio.ini

#include <Arduino.h>
#include <AccelStepper.h>

// Right motor (container 0) — PCB wiring
#define AIN1R 21
#define AIN2R 47
#define BIN1R 45
#define BIN2R 48
#define ENABLE_R 14

#define ENABLE_M 13
#define ENABLE_L 12

AccelStepper stepper(AccelStepper::FULL4WIRE, AIN1R, AIN2R, BIN1R, BIN2R);

const float MOTOR_MAX_SPEED    = 300.0f;
const float MOTOR_ACCELERATION = 100.0f;
const float STEPS_PER_GRAM     = 320.0f;          // CALIBRATE: steps / gram
const unsigned long DISPENSE_TIMEOUT_MS = 15000;   // 15 s hard timeout


// void setup()
// {
//   Serial.begin(115200);
//   delay(1500);
//   Serial.println("\n=== Motor Test (no BLE) ===");

//   pinMode(ENABLE_R, OUTPUT);
//   digitalWrite(ENABLE_R, HIGH);
//   pinMode( ENABLE_M, OUTPUT);
//   digitalWrite(ENABLE_M, LOW);
//   pinMode( ENABLE_L, OUTPUT);
//   digitalWrite(ENABLE_L, LOW);
//   Serial.println("ENABLE_R → HIGH");

//   stepper.setMaxSpeed(800);
//   stepper.setAcceleration(200);
//   // stepper.moveTo(STEPS_PER_GRAM);

//   Serial.printf("Moving to %ld steps (maxSpeed=%.0f, accel=%.0f)\n", STEPS_PER_GRAM, MOTOR_MAX_SPEED, MOTOR_ACCELERATION);
// }

// void loop()
// {
//   stepper.run();

//   // // When target reached, reverse direction
//   // if (stepper.distanceToGo() == 0)
//   // {
//   //   long newTarget = -stepper.currentPosition();
//   //   Serial.printf("Reached target. Reversing → %ld\n", newTarget);
//   //   stepper.moveTo(newTarget);
//   //   delay(500); // brief pause between reversals
//   // }
// }


void spinMotor() {
    Serial.println("Spin command received via BlueTooth");

    stepper.setCurrentPosition(0);
    stepper.moveTo(200);
    // stepperMiddle.setCurrentPosition(0);
    // stepperMiddle.moveTo(200);
}

void setup()
{
    Serial.begin(115200);
    delay(1500);

    Serial.println("Motor Test Starting...");

    // Enable TB6612 driver
    pinMode(ENABLE_R, OUTPUT);
    digitalWrite(ENABLE_R, LOW);
    pinMode(ENABLE_M, OUTPUT);
    digitalWrite(ENABLE_M, LOW);
    pinMode(ENABLE_L, OUTPUT);
    digitalWrite(ENABLE_L, HIGH);
    Serial.println("ENABLE_L → HIGH");

    // Stepper Motors Setup
    stepper.setMaxSpeed(800);
    stepper.setAcceleration(200);

    // Start first move
    stepper.setCurrentPosition(0);
    stepper.moveTo(100);
    Serial.println("Moving to 100 steps...");
}


void loop()
{
    stepper.run();
}