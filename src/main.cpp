#include <Arduino.h>
#include "config.h"

void setup() {
  Serial.begin(115200);
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("RO monitor hello %lu\n", n++);
  delay(1000);
}
