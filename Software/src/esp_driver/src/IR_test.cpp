// #include <Arduino.h>
// #include <Wire.h>
// #include "Adafruit_VL53L0X.h"

// Adafruit_VL53L0X lox;

// struct Sensor { const char* name; int sda; int scl; float emptyMm; float fullMm; };
// Sensor sensors[] = {
//   {"left",   35, 36, 83.6f,  50.7f},   // chili pepper
//   {"middle", 38, 37, 110.9f, 91.2f},  // MSG (calibrated: 109.2mm→80%)
//   {"right",  40, 39,  73.8f,  45.0f},  // salt (calibrated: 66.2mm→85%)
// };

// // const IrSensorConfig IR_SENSORS[3] = {
// //   {"left",   35, 36, 83.6f,  50.7f},   // chili pepper
// //   {"middle", 38, 37, 110.9f, 91.2f},  // MSG (calibrated: 109.2mm→80%)
// //   {"right",  40, 39,  73.8f,  45.0f},  // salt (calibrated: 66.2mm→85%)
// // };

// void setup() {
//     Serial.begin(115200);
//     delay(1500);
//     Serial.println("IR sensor test");
// }

// void loop() {
//     for (auto& s : sensors) {
//         Wire.end();
//         delay(5);
//         Wire.begin(s.sda, s.scl);
//         Wire.setClock(100000);
//         delay(5);

//         if (!lox.begin(0x29, false, &Wire)) {
//             Serial.printf("%s: init failed\n", s.name);
//             continue;
//         }

//         VL53L0X_RangingMeasurementData_t m;
//         lox.rangingTest(&m, false);

//         if (m.RangeStatus == 4)
//             Serial.printf("%s: out of range\n", s.name);
//         else
//             Serial.printf("%s: %d mm\n", s.name, m.RangeMilliMeter);
//     }
//     Serial.println();
//     delay(1000);
    
// }

#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

struct SensorConfig {
  const char* name;
  int sda;
  int scl;
  float emptyReading;
  float fullReading;
};

SensorConfig sensors[] = {
  {"Left",   35, 36, 153.5f, 58.0f},
  {"Middle", 38, 37, 111, 96},
  {"Right",  40, 39, 0, 32}
};

const int NUM_SENSORS = 3;

bool readSensor(const SensorConfig& sensor) {
  // Switch I2C pins to this sensor
  Wire.end();
  delay(10);

  Wire.begin(sensor.sda, sensor.scl);
  Wire.setClock(100000);
  delay(10);

  // Reinitialize VL53L0X on this I2C bus
  if (!lox.begin(0x29, false, &Wire)) {
    Serial.print(sensor.name);
    Serial.println(": Failed to boot VL53L0X");
    return false;
  }

  VL53L0X_RangingMeasurementData_t measure;

  int sampleSize = 20;
  float sum = 0;
  int validCount = 0;
  int outOfRange = 0;

  for (int i = 0; i < sampleSize; i++) {
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
      sum += measure.RangeMilliMeter;
      validCount++;
    } else {
      outOfRange++;
    }

    delay(5);
  }

  Serial.print(sensor.name);
  Serial.print(": ");

  if (outOfRange >= sampleSize * 0.4 || validCount == 0) {
    Serial.println("out of range");
    return false;
  }

  float reading = sum / validCount;

  Serial.print("Distance: ");
  Serial.print(reading);
  Serial.print(" mm, ");

  float percentage = (reading - sensor.emptyReading) /
                     (sensor.fullReading - sensor.emptyReading);

  if (percentage <= 0.05) {
    percentage = 0.0;
  } else if (percentage >= 0.95) {
    percentage = 1.0;
  }

  Serial.print("Percentage: ");
  Serial.print(percentage * 100.0);
  Serial.println("%");

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("Starting 3 VL53L0X sensor reader");
}

void loop() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    readSensor(sensors[i]);
    delay(100);
  }

  Serial.println();
  delay(500);
}