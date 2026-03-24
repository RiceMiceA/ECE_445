#include <Arduino.h>


//accelstepper constant speed example has been altered and is being used
// not using default 4 wires setup, but instead using step, direction, and enable pins
// using TB6600 4A 9-42V stepper driver at 6400 pulses/rev (32 microsteps)

#include <AccelStepper.h>

#include <Arduino.h>
#include <AccelStepper.h>

#define AIN1 32
#define AIN2 14
#define BIN1 15
#define BIN2 33

AccelStepper stepper(
  AccelStepper::FULL4WIRE,
  AIN1, BIN1, AIN2, BIN2
);

void setup()
{
  Serial.begin(9600);

  stepper.setMaxSpeed(800);
  stepper.setAcceleration(200);
  stepper.moveTo(200); // one revolution
}

void loop()
{
  stepper.run();
}