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
const float MOTOR_SHUTDOWN_LEVEL_MM = 550.0F;
const int MOTOR_MAGNETIZATION_PWM = 55;
const unsigned long LEVEL_SAMPLE_INTERVAL_MS = 250;
const int DEFAULT_ON_OFF_HYSTERESIS_MM = 10;
const int DEFAULT_ON_OFF_PWM = 180;
const float DEFAULT_P_GAIN = 1.0F;
const float MIN_P_GAIN = 0.0F;
const float MAX_P_GAIN = 10.0F;
const float DEFAULT_PI_INTEGRAL_GAIN = 0.1F;
const float MIN_PI_INTEGRAL_GAIN = 0.0F;
const float MAX_PI_INTEGRAL_GAIN = 2.0F;

String serialLine;
unsigned long lastLevelSampleTime;
int desiredLevelMm = 250;
int onOffHysteresisMm = DEFAULT_ON_OFF_HYSTERESIS_MM;
int onOffPwm = DEFAULT_ON_OFF_PWM;
float pGain = DEFAULT_P_GAIN;
float piIntegralGain = DEFAULT_PI_INTEGRAL_GAIN;
float piIntegral = 0.0F;
bool onOffPumpRunning = false;
int currentMotorPwm = 0;
float currentPContribution = 0.0F;
float currentIContribution = 0.0F;

enum RegulationMode {
  MODE_OFF,
  MODE_MANUAL,
  MODE_ON_OFF,
  MODE_P,
  MODE_PI,
};

RegulationMode regulationMode = MODE_OFF;

String regulationModeLabel() {
  switch (regulationMode) {
    case MODE_OFF:
      return "OFF";
    case MODE_ON_OFF:
      return "On/Off";
    case MODE_P:
      return "P";
    case MODE_PI:
      return "PI";
    case MODE_MANUAL:
    default:
      return "Manuell";
  }
}

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

int writeMotorPwm(int requestedPwm) {
  const int safeMotorPwm = regulationMode == MODE_OFF || readMeasuredLevel() > MOTOR_SHUTDOWN_LEVEL_MM
    ? 0
    : requestedPwm;

  analogWrite(MOTOR_PWM_PIN, safeMotorPwm);
  currentMotorPwm = safeMotorPwm;
  return safeMotorPwm;
}

void updateManualMotorPwm() {
  currentPContribution = 0.0F;
  currentIContribution = 0.0F;
  const int potentiometerValue = analogRead(MOTOR_POTENTIOMETER_PIN);
  const int requestedMotorPwm = potentiometerValue * 255L / ADC_MAX_VALUE;
  const int motorPwm = writeMotorPwm(requestedMotorPwm);

  Serial.print("POT:");
  Serial.print(potentiometerValue);
  Serial.print('|');
  Serial.println(motorPwm);
}

void updateOnOffMotorPwm(float measuredLevel) {
  currentPContribution = 0.0F;
  currentIContribution = 0.0F;
  const float lowerLimit = desiredLevelMm - onOffHysteresisMm;
  const float upperLimit = desiredLevelMm + onOffHysteresisMm;

  if (measuredLevel < lowerLimit) {
    onOffPumpRunning = true;
  } else if (measuredLevel >= upperLimit) {
    onOffPumpRunning = false;
  }

  const int requestedMotorPwm = onOffPumpRunning ? onOffPwm : 0;
  writeMotorPwm(requestedMotorPwm);
}

void updateProportionalMotorPwm(float measuredLevel) {
  const float levelError = desiredLevelMm - measuredLevel;
  currentPContribution = pGain * levelError;
  currentIContribution = 0.0F;
  const int requestedMotorPwm = constrain(
    static_cast<int>(MOTOR_MAGNETIZATION_PWM + levelError * pGain),
    0,
    255);

  writeMotorPwm(requestedMotorPwm);
}

void resetPiIntegral() {
  piIntegral = 0.0F;
}

void updatePiMotorPwm(float measuredLevel, float elapsedSeconds) {
  const float levelError = desiredLevelMm - measuredLevel;

  if (piIntegralGain > 0.0F) {
    piIntegral += levelError * elapsedSeconds;
    const float maximumIntegral = 255.0F / piIntegralGain;
    piIntegral = constrain(piIntegral, -maximumIntegral, maximumIntegral);
  }

  currentPContribution = pGain * levelError;
  currentIContribution = piIntegralGain * piIntegral;
  const float requestedMotorPwm = MOTOR_MAGNETIZATION_PWM
    + currentPContribution
    + currentIContribution;
  writeMotorPwm(constrain(static_cast<int>(requestedMotorPwm), 0, 255));
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

void showRegulationStatus(float levelError) {
  showLcdLines("Reg: " + regulationModeLabel(), "Fel: " + String(levelError, 1) + " mm");
}

void handleSerialLine(String line) {
  line.trim();

  if (line.startsWith("MODE:")) {
    const String mode = line.substring(5);

    if (mode == "off") {
      regulationMode = MODE_OFF;
    } else if (mode == "manual") {
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

  resetPiIntegral();
    writeMotorPwm(0);
    showRegulationStatus(desiredLevelMm - readMeasuredLevel());
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

  if (line.startsWith("ONOFF:")) {
    const String payload = line.substring(6);
    const int separatorIndex = payload.indexOf('|');
    const String hysteresisText = separatorIndex >= 0 ? payload.substring(0, separatorIndex) : "";
    const String pwmText = separatorIndex >= 0 ? payload.substring(separatorIndex + 1) : "";
    const int hysteresis = hysteresisText.toInt();
    const int pwm = pwmText.toInt();

    if (hysteresisText.length() > 0 && pwmText.length() > 0
        && hysteresis >= 0 && hysteresis <= 250
        && pwm >= 0 && pwm <= 255
        && String(hysteresis) == hysteresisText
        && String(pwm) == pwmText) {
      onOffHysteresisMm = hysteresis;
      onOffPwm = pwm;
      Serial.println("ONOFF:" + String(onOffHysteresisMm) + "|" + String(onOffPwm));
    } else {
      Serial.println("ONOFF:ERROR");
    }
    return;
  }

  if (line.startsWith("PGAIN:")) {
    const String gainText = line.substring(6);
    const float gain = gainText.toFloat();

    if (gainText.length() > 0 && gain >= MIN_P_GAIN && gain <= MAX_P_GAIN) {
      pGain = gain;
      Serial.println("PGAIN:" + String(pGain, 1));
    } else {
      Serial.println("PGAIN:ERROR");
    }
    return;
  }

  if (line.startsWith("PIGAIN:")) {
    const String gainText = line.substring(7);
    const float gain = gainText.toFloat();

    if (gainText.length() > 0 && gain >= MIN_PI_INTEGRAL_GAIN && gain <= MAX_PI_INTEGRAL_GAIN) {
      piIntegralGain = gain;
      resetPiIntegral();
      Serial.println("PIGAIN:" + String(piIntegralGain, 2));
    } else {
      Serial.println("PIGAIN:ERROR");
    }
    return;
  }

  if (line.startsWith("MOTOR:")) {
    const String valueText = line.substring(6);
    const int pwmValue = valueText.toInt();

    if (valueText.length() > 0 && pwmValue >= 0 && pwmValue <= 255 && String(pwmValue) == valueText) {
      const int safeMotorPwm = writeMotorPwm(pwmValue);
      Serial.println("MOTOR:" + String(safeMotorPwm));
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
    const float elapsedSeconds = (currentTime - lastLevelSampleTime) / 1000.0F;
    lastLevelSampleTime = currentTime;
    const float measuredLevel = readMeasuredLevel();
    const float levelError = desiredLevelMm - measuredLevel;

    Serial.print("LEVEL:");
    Serial.print(measuredLevel, 1);
    Serial.print('|');
    Serial.print(desiredLevelMm);
    Serial.print('|');
    Serial.println(levelError, 1);
    showRegulationStatus(levelError);

    if (regulationMode == MODE_MANUAL) {
      updateManualMotorPwm();
    } else if (regulationMode == MODE_ON_OFF) {
      updateOnOffMotorPwm(measuredLevel);
    } else if (regulationMode == MODE_P) {
      updateProportionalMotorPwm(measuredLevel);
    } else if (regulationMode == MODE_PI) {
      updatePiMotorPwm(measuredLevel, elapsedSeconds);
    }

    Serial.print("OUTPUT:");
    Serial.print(currentMotorPwm);
    Serial.print('|');
    Serial.print(currentPContribution, 1);
    Serial.print('|');
    Serial.println(currentIContribution, 1);
  }
}