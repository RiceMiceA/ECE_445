// LOAD CELL
#include "HX711.h"

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 2;
const int LOADCELL_SCK_PIN = 1;

HX711 scale;

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
    float reading = scale.get_units(50);
    if(abs(reading) <= 0.05){
        reading = 0.00;
    }
    Serial.print("HX711 reading: ");
    Serial.println(reading);
  } else {
    Serial.println("HX711 not found.");
  }

  delay(1000);
  
}