//accelstepper constant speed example has been altered and is being used
// not using default 4 wires setup, but instead using step, direction, and enable pins
// using TB6600 4A 9-42V stepper driver at 6400 pulses/rev (32 microsteps)

//Put into terminal
//iwr -Uri "http://192.168.196.86/spin" -Method POST

#include <Arduino.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <WebServer.h>

#define AIN1R 32
#define AIN2R 14
#define BIN1R 15
#define BIN2R 33

#define AIN1M 32
#define AIN2M 14
#define BIN1M 15
#define BIN2M 33

AccelStepper stepperRight(
  AccelStepper::FULL4WIRE,
  AIN1R, AIN2R, BIN1R, BIN2R
);

AccelStepper stepperMiddle(
    AccelStepper::FULL4WIRE,
    AIN1M, AIN2M, BIN1M, BIN2M
);

WebServer server(80);


const char* ssid = "Triangle";
const char* password = "houserules";
void connectWiFi() {
  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void spinMotor() {
  Serial.println("Spin command received");

  stepperMiddle.setCurrentPosition(0);
  stepperMiddle.moveTo(200);

  server.send(200, "text/plain", "The motor has been spun!");
}

void setup()
{
    Serial.begin(115200);
    stepperMiddle.setMaxSpeed(800);
    stepperMiddle.setAcceleration(200);
    stepperRight.setMaxSpeed(800);
    stepperRight.setAcceleration(200);
    connectWiFi();
    server.on("/spin", HTTP_POST, spinMotor);
    server.begin();

    // stepperRight.setCurrentPosition(0);
    // stepperRight.moveTo(200); // one revolution

    // stepperMiddle.setCurrentPosition(0);
    // stepperMiddle.moveTo(200); // one revolution
}


void loop()
{
  //stepperRight.run();
  //delay(10000);
  server.handleClient();
  stepperMiddle.run();
}