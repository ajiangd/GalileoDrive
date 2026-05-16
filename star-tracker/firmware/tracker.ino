#include <AccelStepper.h>
#include <MPU6050.h>
#include <QMC5883LCompass.h>
#include <Wire.h>

// ── Hardware config ──────────────────────────────────────────────────────────
// A4988/TMC2208 drivers: STEP and DIR pins for each axis
#define AZ_STEP_PIN   3
#define AZ_DIR_PIN    4
#define ALT_STEP_PIN  6
#define ALT_DIR_PIN   7

// LED pins
const int LED_BLUE   = A0;  // serial byte received
const int LED_YELLOW = A1;  // azimuth correcting
const int LED_GREEN  = A2;  // altitude correcting
const int LED_RED    = A3;  // error / below horizon

// Motor parameters (match config.yaml)
const float STEPS_PER_REV = 200.0;
const float MICROSTEP     = 16.0;
const float GEAR_RATIO_AZ  = 1.0;
const float GEAR_RATIO_ALT = 1.0;

const float STEPS_PER_DEG_AZ  = (STEPS_PER_REV * MICROSTEP * GEAR_RATIO_AZ)  / 360.0;
const float STEPS_PER_DEG_ALT = (STEPS_PER_REV * MICROSTEP * GEAR_RATIO_ALT) / 360.0;

// ── PID ──────────────────────────────────────────────────────────────────────
struct PID {
  float kp, ki, kd;
  float integral;
  float prev_error;
  static const float MAX_INTEGRAL = 180.0;

  float compute(float error, float dt) {
    integral += error * dt;
    integral = constrain(integral, -MAX_INTEGRAL, MAX_INTEGRAL);
    float derivative = (dt > 0) ? (error - prev_error) / dt : 0.0;
    prev_error = error;
    return kp * error + ki * integral + kd * derivative;
  }
};

PID pid_az  = {2.0, 0.05, 0.5, 0.0, 0.0};
PID pid_alt = {2.0, 0.05, 0.5, 0.0, 0.0};

// ── Motors ───────────────────────────────────────────────────────────────────
AccelStepper motor_az (AccelStepper::DRIVER, AZ_STEP_PIN,  AZ_DIR_PIN);
AccelStepper motor_alt(AccelStepper::DRIVER, ALT_STEP_PIN, ALT_DIR_PIN);

// ── Sensors ──────────────────────────────────────────────────────────────────
MPU6050        mpu;
QMC5883LCompass compass;

// ── State ────────────────────────────────────────────────────────────────────
float targetAzimuth  = 0.0;
float targetAltitude = 0.0;
bool  hasTarget      = false;
bool  stopRequested  = false;

char serialBuffer[64];
byte bufferIndex = 0;

unsigned long lastMillis = 0;

// ── Serial parsing ───────────────────────────────────────────────────────────
void parseMessage() {
  // Expected format: "AZ:180.50 ALT:45.30" or "CMD:STOP"
  char* token = strtok(serialBuffer, " ");
  while (token != nullptr) {
    char* colon = strchr(token, ':');
    if (colon != nullptr) {
      *colon = '\0';
      char* key   = token;
      char* value = colon + 1;

      if (strcmp(key, "AZ") == 0) {
        targetAzimuth = atof(value);
      } else if (strcmp(key, "ALT") == 0) {
        targetAltitude = atof(value);
        hasTarget = true;
        stopRequested = false;
      } else if (strcmp(key, "CMD") == 0 && strcmp(value, "STOP") == 0) {
        stopRequested = true;
        hasTarget = false;
      }
    }
    token = strtok(nullptr, " ");
  }
}

void readSerialNonBlocking() {
  while (Serial.available() > 0) {
    digitalWrite(LED_BLUE, HIGH);
    char inChar = Serial.read();
    digitalWrite(LED_BLUE, LOW);

    if (inChar == '\n') {
      serialBuffer[bufferIndex] = '\0';
      bufferIndex = 0;
      parseMessage();
    } else {
      if (bufferIndex < sizeof(serialBuffer) - 1) {
        serialBuffer[bufferIndex++] = inChar;
      } else {
        bufferIndex = 0;  // overflow — discard and reset
      }
    }
  }
}

// ── Sensors ──────────────────────────────────────────────────────────────────
float readAltitude() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float Ax = ax / 16384.0;
  float Ay = ay / 16384.0;
  float Az = az / 16384.0;
  return atan2(Ax, sqrt(Ay * Ay + Az * Az)) * 180.0 / PI;
}

float readAzimuth() {
  compass.read();
  int heading = compass.getAzimuth();
  if (heading < 0)   heading += 360;
  if (heading > 360) heading -= 360;
  return (float)heading;
}

// ── Angular error with wrap-around ───────────────────────────────────────────
float angularError(float current, float target) {
  return fmod((target - current + 540.0), 360.0) - 180.0;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu.initialize();
  compass.init();

  pinMode(LED_BLUE,   OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  digitalWrite(LED_BLUE,   LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_RED,    LOW);

  motor_az.setMaxSpeed(2000);
  motor_az.setAcceleration(500);
  motor_alt.setMaxSpeed(2000);
  motor_alt.setAcceleration(500);

  lastMillis = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  motor_az.run();
  motor_alt.run();

  readSerialNonBlocking();

  unsigned long now = millis();
  float dt = (now - lastMillis) / 1000.0;
  lastMillis = now;

  if (stopRequested) {
    motor_az.stop();
    motor_alt.stop();
    digitalWrite(LED_RED, HIGH);
    return;
  }
  digitalWrite(LED_RED, LOW);

  if (!hasTarget) return;

  float currentAlt = readAltitude();
  float currentAz  = readAzimuth();

  float errAz  = angularError(currentAz,  targetAzimuth);
  float errAlt = angularError(currentAlt, targetAltitude);

  float outAz  = pid_az.compute(errAz,  dt);
  float outAlt = pid_alt.compute(errAlt, dt);

  motor_az.move( (long)(outAz  * STEPS_PER_DEG_AZ));
  motor_alt.move((long)(outAlt * STEPS_PER_DEG_ALT));

  digitalWrite(LED_YELLOW, abs(errAz)  > 0.5 ? HIGH : LOW);
  digitalWrite(LED_GREEN,  abs(errAlt) > 0.5 ? HIGH : LOW);

  // Send status back to Python
  char buf[80];
  snprintf(buf, sizeof(buf),
    "AZ:%.2f ALT:%.2f ERR_AZ:%.2f ERR_ALT:%.2f",
    currentAz, currentAlt, errAz, errAlt);
  Serial.println(buf);
}
