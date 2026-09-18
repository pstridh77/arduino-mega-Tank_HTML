#include <Arduino.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

const byte LCD_BACKLIGHT_PIN = 10;
const byte MOTOR_DIRECTION_PIN = 13;
const byte MOTOR_PWM_PIN = 11;
const byte MOTOR_POTENTIOMETER_PIN = A2;
const byte LEVEL_SENSOR_PIN = A4;

const float ADC_MAX_VALUE = 1023.0F;
const float ADC_REFERENCE_VOLTAGE = 5.0F;
const float SENSOR_RANGE_MM = 500.0F;
const float SENSOR_FULL_SCALE_VOLTAGE = 5.0F;
const float MAX_LEVEL_MM = 500.0F;
const float MIN_LEVEL_MM = 60.0F;
const unsigned long LEVEL_SAMPLE_INTERVAL_MS = 250;

String serialLine;
unsigned long lastLevelSampleTime;
int desiredLevelMm = 250;

enum RegulationMode {
  MODE_MANUAL,
  MODE_ON_OFF,
  MODE_P,
  MODE_PI,
};

RegulationMode regulationMode = MODE_MANUAL;

float readMeasuredLevel() {
  const int sensorValue = analogRead(LEVEL_SENSOR_PIN);
  const float sensorVoltage = sensorValue * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE;
  const float sensorDistance = sensorVoltage * SENSOR_RANGE_MM / SENSOR_FULL_SCALE_VOLTAGE;

  return MAX_LEVEL_MM + MIN_LEVEL_MM - sensorDistance;
}

void reportMeasuredLevel() {
  const float measuredLevel = readMeasuredLevel();
  const float levelError = desiredLevelMm - measuredLevel;

  Serial.print("LEVEL:");
  Serial.print(measuredLevel, 1);
  Serial.print('|');
  Serial.print(desiredLevelMm);
  Serial.print('|');
  Serial.println(levelError, 1);
}

void updateManualMotorPwm() {
  const int potentiometerValue = analogRead(MOTOR_POTENTIOMETER_PIN);
  const int motorPwm = potentiometerValue * 255L / ADC_MAX_VALUE;

  analogWrite(MOTOR_PWM_PIN, motorPwm);
  Serial.print("POT:");
  Serial.print(potentiometerValue);
  Serial.print('|');
  Serial.println(motorPwm);
}

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

  if (line.startsWith("MODE:")) {
    const String mode = line.substring(5);

    if (mode == "manual") {
      regulationMode = MODE_MANUAL;
    } else if (mode == "on-off") {
      regulationMode = MODE_ON_OFF;
    } else if (mode == "p") {
      regulationMode = MODE_P;
    } else if (mode == "pi") {
      regulationMode = MODE_PI;
    } else {
      Serial.println("MODE:ERROR");
      return;
    }

    Serial.println("MODE:" + mode);
    return;
  }

  if (line.startsWith("SETPOINT:")) {
    const String valueText = line.substring(9);
    const int setpointValue = valueText.toInt();

    if (valueText.length() > 0 && setpointValue >= 0 && setpointValue <= 500 && String(setpointValue) == valueText) {
      desiredLevelMm = setpointValue;
      Serial.println("SETPOINT:" + String(desiredLevelMm));
      reportMeasuredLevel();
    } else {
      Serial.println("SETPOINT:ERROR");
    }
    return;
  }

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
  analogWrite(LCD_BACKLIGHT_PIN, 100);

  lcd.begin(16, 2);
  showLcdLines("FMTS", "Start Tank HTML");
  
  Serial.begin(115200);
  Serial.println("READY");
  reportMeasuredLevel();
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

  const unsigned long currentTime = millis();
  if (currentTime - lastLevelSampleTime >= LEVEL_SAMPLE_INTERVAL_MS) {
    lastLevelSampleTime = currentTime;
    reportMeasuredLevel();

    if (regulationMode == MODE_MANUAL) {
      updateManualMotorPwm();
    }
  }
}