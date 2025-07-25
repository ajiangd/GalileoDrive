#include <AccelStepper.h>
#include <MPU6050.h>
#include <QMC5883LCompass.h>
#include <Wire.h>
//#include <math.h>

MPU6050 mpu;
QMC5883LCompass compass;

// Define motor interface type
#define MOTOR_INTERFACE_TYPE 4  // 4-pin full-step mode

// Stepper 1 (Tilt): IN4, IN3, IN2, IN1 = 8, 9, 10, 11
AccelStepper motor1_alt(MOTOR_INTERFACE_TYPE, 11, 9, 10, 8); // IN1, IN3, IN2, IN4

// Stepper 2 (Heading): IN4, IN3, IN2, IN1 = 4, 5, 6, 7
AccelStepper motor2_azi(MOTOR_INTERFACE_TYPE, 7, 5, 6, 4);   // IN1, IN3, IN2, IN4

// Stepper status
bool motor1_altActive = true;
bool motor2_aziActive = true;

// LED Pins
const int ledY = A0;
const int ledB = A1;  
const int ledR = A2;
const int ledG = A3;

char serialBuffer[50];
byte bufferIndex = 0;

bool objectReady = true;
bool stopRequested = false;

float objectAzimuth = 0.0;
float objectAltitude = 0.0;

void readSerialNonBlocking() {
  while (Serial.available() > 0) {
    
    digitalWrite(ledB, HIGH);
    digitalWrite(ledB, LOW);
    
    char inChar = Serial.read();
    
    if (inChar == '\n') {
      serialBuffer[bufferIndex] = '\0'; // null-terminate string
      
      // Reset for next message (a different object selected)
      bufferIndex = 0;
      
      // Check if it is a STOP command
      if (strcmp(serialBuffer, "STOP") == 0) {
        stopRequested = true;
        objectReady = false;
        return;
      }
      
      // Try to parse two floats from butter
      char* ptr = strtok(serialBuffer, " ");
      if (ptr != nullptr) {
        
        float alt = atof(ptr);
        ptr = strtok(nullptr, " ");
        
        if (ptr != nullptr) {
          
          float az = atof(ptr);
          objectAltitude = alt;
          objectAzimuth = az;
          objectReady = true;
          stopRequested = false;
          return;
          
        }
      }

      
    } else {  // If parsing fails, discard message
        //add char to buffer if space
        if (bufferIndex < sizeof(serialBuffer) - 1) {
          serialBuffer[bufferIndex++] = inChar;
        } else {
            bufferIndex = 0; //overflow safety
        }
    }
  }
}

//float readInclinometer();
//float readMagnetometer();



void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu.initialize();
  compass.init();
  
  pinMode(ledB, OUTPUT);
  pinMode(ledR, OUTPUT);
  pinMode(ledY, OUTPUT);
  pinMode(ledG, OUTPUT);
  digitalWrite(ledB, LOW);
  digitalWrite(ledR, LOW);
  digitalWrite(ledY, LOW);
  digitalWrite(ledG, LOW);

  motor1_alt.setMaxSpeed(1000);     // up to 800 steps/sec is safe
  motor1_alt.setAcceleration(200); // up to 1500 steps/sec is still safe

  motor2_azi.setMaxSpeed(1000);
  motor2_azi.setAcceleration(200);
  
}


void loop() {
  // Always allow motor stepping
  motor1_alt.run();  
  motor2_azi.run();
  
  readSerialNonBlocking(); 
  //Serial.println(objectReady);

  //Serial.println(stopRequested);
  
  if (stopRequested) {
      // Gracefully decelerate to stop
      motor1_alt.stop();
      motor2_azi.stop();
    
      // OR
      // motor1_alt.setCurrentPosition(0);   // Optional reset
      // motor2_azi.setCurrentPosition(0);   // Optional reset
  }

  float currentAngle = readInclinometer();
  float currentHeading = readMagnetometer();
      
  //if (objectReady) {
    
    if (angularError(currentHeading, objectAzimuth) > 50) {
      digitalWrite(ledY, HIGH);
      digitalWrite(ledY, LOW);
      motor2_azi.move(500);
    } else {
      motor2_azi.stop();
    }


    if (angularError(currentAngle, objectAltitude) > 20) {
      digitalWrite(ledG, HIGH);
      digitalWrite(ledG, LOW);        
      motor1_alt.move(500);
    } else {
      motor1_alt.stop();
    }

    char buffer[50];
    sprintf(buffer, "... Angle delta: %d | heading delta: %d", angularError(currentAngle, objectAltitude), angularError(currentHeading, objectAzimuth));
    Serial.println(buffer);
    
    objectReady = false;

      
  //}

    sprintf(buffer, "Tracker Angle: %d | Heading: %d", currentAngle, currentHeading);
    Serial.println(buffer);
      
}


float angularError(float current, float target)
{
  float delta = fmod((target - current + 540.0), 360.0) - 180.0;
  return delta; 
}


float readInclinometer() {

  int16_t ax, ay, az;
  //int16_t gx, gy, gz; // not using gyro data

  // Read accelerometer and gyroscope values
  //mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  mpu.getAcceleration(&ax, &ay, &az);

  float Ax = ax/16384.0;
  float Ay = ay/16384.0;
  float Az = az/16384.0;

  float updatedAngle = atan2(Ax, sqrt(Ay * Ay + Az * Az)) * 180 / PI;
  //float roll  = atan2(Ay, sqrt(Ax * Ax + Az * Az)) * 180 / PI;
  
  //Serial.print("Accel X: "); Serial.print(ax);
  //Serial.print(" Y: "); Serial.print(ay);
  //Serial.print(" Z: "); Serial.print(az);
  //Serial.print(" | Gyro X: "); Serial.print(gx);
  //Serial.print(" Y: "); Serial.print(gy);
  //Serial.print(" Z: "); Serial.println(gz);

  //Serial.print("Pitch: "); Serial.print(pitch);
  //Serial.print("| Roll: "); Serial.println(roll);

  return updatedAngle;

}



float readMagnetometer() {
  compass.read();

  //int x = compass.getX();
  //int y = compass.getY();
  //int z = compass.getZ();
  int updatedHeading = compass.getAzimuth();

  if (updatedHeading < 0)   updatedHeading += 360;
  if (updatedHeading > 360) updatedHeading -= 360;
 
  //float heading_truenorth = atan2(y, x) * 180.0 / PI;
  //if (heading_truenorth < 0) heading_truenorth += 360;

  // Apply magnetic declination
  //float declination = 14.4; // assumed value for my current location for now

  //heading_truenorth += declination;
  //if (heading_truenorth < 0) heading_truenorth += 360;
  //if (heading_truenorth > 360) heading_truenorth -= 360;
  

  //Serial.print("X: "); Serial.print(x);
  //Serial.print(" Y: "); Serial.print(y);
  //Serial.print(" Z: "); Serial.print(z);
  
  //Serial.print(" heading: "); Serial.print(heading);
  //Serial.print(" heading_truenorth: "); Serial.println(heading_truenorth);

  return updatedHeading;
 }
