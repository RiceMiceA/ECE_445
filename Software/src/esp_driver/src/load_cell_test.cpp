// LOAD CELL
#include "HX711.h"

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN = 1;

HX711 scale;

const uint8_t HX711_SAMPLES_PER_READ = 5;
const unsigned long LOOP_DELAY_MS = 100;

void setup() {
  Serial.begin(115200);

  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);
  
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.tare();
  scale.set_scale((1250.0)/1.0); //(reading)/ actual weight
}

void loop() {

  if (scale.is_ready()) {
    float reading = scale.get_units(HX711_SAMPLES_PER_READ);
    if(abs(reading) <= 0.05){
        reading = 0.00;
    }
    Serial.print("HX711 reading: ");
    Serial.println(reading);
  } else {
    Serial.println("HX711 not found.");
  }

  delay(LOOP_DELAY_MS);
  
}