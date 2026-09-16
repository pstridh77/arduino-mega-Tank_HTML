#include <Arduino.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  Serial.println("READY");
}

void loop() {
  if (Serial.available() == 0) {
    return;
  }

  const char command = Serial.read();

  if (command == '1') {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("LED:ON");
  } else if (command == '0') {
    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("LED:OFF");
  }
}