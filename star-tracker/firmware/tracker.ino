#include <AccelStepper.h>
#include <MPU6050.h>
#include <QMC5883LCompass.h>
#include <Wire.h>

// ── Hardware config ───────────────────────────────────────────────────────────
#define AZ_STEP_PIN   3
#define AZ_DIR_PIN    4
#define ALT_STEP_PIN  6
#define ALT_DIR_PIN   7

const int LED_BLUE   = A0;  // serial byte received
const int LED_YELLOW = A1;  // azimuth correcting
const int LED_GREEN  = A2;  // altitude correcting
const int LED_RED    = A3;  // fault / stopped

// Motor parameters — keep in sync with config.yaml manually (not sent over serial)
const float STEPS_PER_REV  = 200.0;
const float MICROSTEP      = 16.0;
const float GEAR_RATIO_AZ  = 1.0;
const float GEAR_RATIO_ALT = 1.0;

const float STEPS_PER_DEG_AZ  = (STEPS_PER_REV * MICROSTEP * GEAR_RATIO_AZ)  / 360.0;
const float STEPS_PER_DEG_ALT = (STEPS_PER_REV * MICROSTEP * GEAR_RATIO_ALT) / 360.0;

// Stop if no valid command received within this many ms.
// Must be greater than tracker.update_interval_s (config.yaml) * 1000, with margin.
// Default gives 10x headroom at the 1s default interval.
const unsigned long COMMAND_TIMEOUT_MS = 10000;

// Send status at most this often to prevent serial TX backlog
const unsigned long STATUS_INTERVAL_MS = 100;

// ── PID ───────────────────────────────────────────────────────────────────────
struct PID {
  float kp, ki, kd;
  float integral;
  float prev_error;
  float filtered_deriv;

  static const float MAX_INTEGRAL = 90.0;
  static const float MAX_OUTPUT   = 10.0;  // degrees of correction per cycle
  static const float DEADBAND     = 0.3;   // degrees — suppress noise near target
  static const float DERIV_ALPHA  = 0.1;   // EMA weight for derivative (lower = smoother)

  void reset() {
    integral       = 0.0;
    prev_error     = 0.0;
    filtered_deriv = 0.0;
  }

  float compute(float error, float dt) {
    if (fabs(error) < DEADBAND) {
      integral = 0.0;  // don't wind up while settled
      return 0.0;
    }
    integral += error * dt;
    integral  = constrain(integral, -MAX_INTEGRAL, MAX_INTEGRAL);

    float raw_deriv    = (dt > 0.0) ? (error - prev_error) / dt : 0.0;
    filtered_deriv     = DERIV_ALPHA * raw_deriv + (1.0 - DERIV_ALPHA) * filtered_deriv;
    prev_error         = error;

    float out = kp * error + ki * integral + kd * filtered_deriv;
    return constrain(out, -MAX_OUTPUT, MAX_OUTPUT);
  }
};

PID pid_az  = {2.0, 0.05, 0.5, 0.0, 0.0, 0.0};
PID pid_alt = {2.0, 0.05, 0.5, 0.0, 0.0, 0.0};

// ── Motors ────────────────────────────────────────────────────────────────────
AccelStepper motor_az (AccelStepper::DRIVER, AZ_STEP_PIN,  AZ_DIR_PIN);
AccelStepper motor_alt(AccelStepper::DRIVER, ALT_STEP_PIN, ALT_DIR_PIN);

// ── Sensors ───────────────────────────────────────────────────────────────────
MPU6050         mpu;
QMC5883LCompass compass;

// ── State ─────────────────────────────────────────────────────────────────────
float targetAzimuth  = 0.0;
float targetAltitude = 0.0;
bool  hasTarget      = false;
bool  stopRequested  = false;

char serialBuffer[64];
byte bufferIndex = 0;

unsigned long lastMillis        = 0;
unsigned long lastCommandMillis = 0;
unsigned long lastStatusMillis  = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────
void holdPosition() {
  // moveTo current position — decelerates cleanly and holds without drift
  motor_az.moveTo(motor_az.currentPosition());
  motor_alt.moveTo(motor_alt.currentPosition());
}

// ── Serial parsing ────────────────────────────────────────────────────────────
void parseMessage() {
  bool  gotAz = false, gotAlt = false;
  float newAz = 0.0,   newAlt = 0.0;

  char* token = strtok(serialBuffer, " ");
  while (token != nullptr) {
    char* colon = strchr(token, ':');
    if (colon != nullptr) {
      *colon    = '\0';
      char* key   = token;
      char* value = colon + 1;

      if (strcmp(key, "CMD") == 0 && strcmp(value, "STOP") == 0) {
        stopRequested = true;
        hasTarget     = false;
        pid_az.reset();
        pid_alt.reset();
        return;
      }
      if (strcmp(key, "AZ") == 0 && *value != '\0') {
        newAz = atof(value);
        gotAz = true;
      } else if (strcmp(key, "ALT") == 0 && *value != '\0') {
        newAlt = atof(value);
        gotAlt = true;
      }
    }
    token = strtok(nullptr, " ");
  }

  // Only commit target when both AZ and ALT arrived in the same message
  if (gotAz && gotAlt) {
    // Reset PID state when target shifts significantly to avoid integral/derivative kick
    if (fabs(newAz - targetAzimuth) > 1.0 || fabs(newAlt - targetAltitude) > 1.0) {
      pid_az.reset();
      pid_alt.reset();
    }
    targetAzimuth     = newAz;
    targetAltitude    = newAlt;
    hasTarget         = true;
    stopRequested     = false;
    lastCommandMillis = millis();
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
        bufferIndex = 0;  // overflow — discard
      }
    }
  }
}

// ── Sensors ───────────────────────────────────────────────────────────────────
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

// Azimuth error: circular wrap [-180, 180]
float azimuthError(float current, float target) {
  return fmod((target - current + 540.0), 360.0) - 180.0;
}

// Altitude error: bounded, not circular
float altitudeError(float current, float target) {
  return constrain(target - current, -90.0, 90.0);
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

  lastMillis        = millis();
  lastCommandMillis = millis();
  lastStatusMillis  = millis();

  Serial.println("READY");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  motor_az.run();
  motor_alt.run();

  readSerialNonBlocking();

  unsigned long now = millis();
  float dt = (now - lastMillis) / 1000.0;
  lastMillis = now;

  // Watchdog: stop if no valid command arrives within timeout
  if (hasTarget && (now - lastCommandMillis > COMMAND_TIMEOUT_MS)) {
    hasTarget     = false;
    stopRequested = true;
    pid_az.reset();
    pid_alt.reset();
  }

  if (stopRequested) {
    holdPosition();
    digitalWrite(LED_RED, HIGH);
    return;
  }
  digitalWrite(LED_RED, LOW);

  if (!hasTarget) return;

  float currentAlt = readAltitude();
  float currentAz  = readAzimuth();

  float errAz  = azimuthError(currentAz,  targetAzimuth);
  float errAlt = altitudeError(currentAlt, targetAltitude);

  float outAz  = pid_az.compute(errAz,  dt);
  float outAlt = pid_alt.compute(errAlt, dt);

  // moveTo() sets absolute position — prevents cumulative runaway from move()
  motor_az.moveTo( motor_az.currentPosition()  + (long)(outAz  * STEPS_PER_DEG_AZ));
  motor_alt.moveTo(motor_alt.currentPosition() + (long)(outAlt * STEPS_PER_DEG_ALT));

  digitalWrite(LED_YELLOW, fabs(errAz)  > 0.5 ? HIGH : LOW);
  digitalWrite(LED_GREEN,  fabs(errAlt) > 0.5 ? HIGH : LOW);

  // Throttle status to prevent TX backlog
  if (now - lastStatusMillis >= STATUS_INTERVAL_MS) {
    lastStatusMillis = now;
    char buf[80];
    snprintf(buf, sizeof(buf),
      "AZ:%.2f ALT:%.2f ERR_AZ:%.2f ERR_ALT:%.2f",
      currentAz, currentAlt, errAz, errAlt);
    Serial.println(buf);
  }
}
