#include <Arduino.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const byte LCD_BACKLIGHT_PIN = 10;
const byte MOTOR_DIRECTION_PIN = 13;
const byte MOTOR_PWM_PIN = 11;

String serialLine;

String fitLcdLine(String text) {
  text.trim();

  if (text.length() > 16) {
    return text.substring(0, 16);
  }

  while (text.length() < 16) {
    text += ' ';
  }

  return text;
}

void showLcdLines(String firstLine, String secondLine) {
  lcd.setCursor(0, 0);
  lcd.print(fitLcdLine(firstLine));
  lcd.setCursor(0, 1);
  lcd.print(fitLcdLine(secondLine));
}

void showMeasurement(String name, String value, String unit) {
  showLcdLines(name, value + " " + unit);
}

void handleSerialLine(String line) {
  line.trim();

  if (line.startsWith("MOTOR:")) {
    const String valueText = line.substring(6);
    const int pwmValue = valueText.toInt();

    if (valueText.length() > 0 && pwmValue >= 0 && pwmValue <= 255 && String(pwmValue) == valueText) {
      analogWrite(MOTOR_PWM_PIN, pwmValue);
      Serial.println("MOTOR:" + String(pwmValue));
    } else {
      Serial.println("MOTOR:ERROR");
    }
    return;
  }

  if (line.startsWith("LCD:")) {
    const String payload = line.substring(4);
    const int separatorIndex = payload.indexOf('|');
    const String firstLine = separatorIndex >= 0 ? payload.substring(0, separatorIndex) : payload;
    const String secondLine = separatorIndex >= 0 ? payload.substring(separatorIndex + 1) : "";

    showLcdLines(firstLine, secondLine);
    Serial.println("LCD:OK");
    return;
  }

  if (line.startsWith("MEAS:")) {
    const String payload = line.substring(5);
    const int firstSeparator = payload.indexOf('|');
    const int secondSeparator = payload.indexOf('|', firstSeparator + 1);

    if (firstSeparator >= 0 && secondSeparator >= 0) {
      const String name = payload.substring(0, firstSeparator);
      const String value = payload.substring(firstSeparator + 1, secondSeparator);
      const String unit = payload.substring(secondSeparator + 1);

      showMeasurement(name, value, unit);
      Serial.println("MEAS:OK");
    } else {
      Serial.println("MEAS:ERROR");
    }
  }
}

void setup() {
  pinMode(MOTOR_DIRECTION_PIN, OUTPUT);
  digitalWrite(MOTOR_DIRECTION_PIN, LOW);

  pinMode(MOTOR_PWM_PIN, OUTPUT);
  analogWrite(MOTOR_PWM_PIN, 0);

  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  analogWrite(LCD_BACKLIGHT_PIN, 80);

  lcd.begin(16, 2);
  showLcdLines("Arduino Mega", "LCD redo");
  delay(800);
  showLcdLines("LCD test", "Kontrast?");

  Serial.begin(115200);
  Serial.println("READY");
}

void loop() {
  while (Serial.available() > 0) {
    const char received = Serial.read();

    if (received == '\n') {
      handleSerialLine(serialLine);
      serialLine = "";
    } else if (received != '\r') {
      serialLine += received;

      if (serialLine.length() > 80) {
        serialLine = "";
        Serial.println("ERROR:LINE_TOO_LONG");
      }
    }
  }
}