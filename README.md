# GalileoDrive - Autonomous Planet-Tracking Telescope
## Project Overview

This project is a low-cost, distributed telescope-tracking system designed to automatically point a telescope toward a selected celestial body. It combines a web interface, astronomical position calculation, wireless embedded communication, sensor feedback, and motor control. A user selects a planet or other supported body from a web page hosted on a Raspberry Pi 5. After a target is selected, the system calculates the target’s current position in the sky as altitude and azimuth values, sends those desired target values to embedded controllers, and uses two stepper motors to physically align the telescope along its altitude and azimuth axes.

The system is divided into four major parts: the Raspberry Pi 5, an ESP32 gateway module, an azimuth control module, and an altitude control module. The Raspberry Pi handles the user interface and high-level astronomy calculations. The gateway ESP32 acts as the communication bridge between the Pi and the two motor/sensor nodes. The azimuth node uses a BMM150 magnetometer to determine compass heading, while the altitude node uses an MPU6050 inclinometer to determine tilt angle. Each axis node compares its real-time sensor reading to the desired target value and drives a 28BYJ-48 stepper motor until the measured value is within an acceptable tolerance of the target.

## Raspberry Pi 5 and Web Interface

The Raspberry Pi 5 serves as the central computer for the system. It runs a Flask web application that provides the user-facing interface, allowing a user to select a target body from a dropdown menu. The Pi also runs the Python tracker program, which performs the astronomy calculations needed to determine where the selected body appears in the sky from the observer’s location.

The web interface is meant to make the tracker usable without manually entering coordinates or serial commands. A user can open the web page, select a target such as the Moon, Sun, or a planet, and allow the backend tracker program to calculate the required pointing direction. The Flask application also supports log streaming, which makes it easier to monitor target values, communication status, and sensor feedback while the system is running.

## Skyfield-Based Target Calculation

The tracker program uses the **Skyfield API**, a Python astronomy library, to load planetary ephemeris data and compute the apparent position of celestial bodies at the current time. In this project, Skyfield is used with the DE421 ephemeris file to access the positions of the Sun, Moon, and planets.

The tracker creates a time object for the current moment, defines the observer’s location using latitude and longitude, and then calculates the selected body’s apparent altitude and azimuth from that observer’s perspective. Altitude represents how high the object is above the horizon, while azimuth represents the compass direction the telescope must face. The user’s location can be obtained through IP-based geolocation, with the option to fall back to manually configured coordinates if needed. Once Skyfield calculates the target altitude and azimuth, the Pi sends those values to the embedded control system as the desired setpoints.

## ESP32 Gateway Module

The gateway ESP32-WROOM board acts as the bridge between the Raspberry Pi and the two sensor/motor ESP32s. The Pi communicates with the gateway over a serial-style command channel, sending target values in a simple text format such as `azimuth altitude`. The gateway reads this incoming data, parses the two floating-point values, and separates them into axis-specific commands.

The azimuth target is sent to the magnetometer-based ESP32, and the altitude target is sent to the inclinometer-based ESP32. Communication between the gateway and the two axis modules is handled using ESP-NOW, a lightweight wireless protocol supported by the ESP32. This allows the two motor controllers to be placed near their respective sensors and motors without requiring long signal wires from the Pi.

## Azimuth Control Module

The azimuth control module is responsible for rotating the telescope left and right along the compass-heading axis. This module consists of an ESP32-WROOM board, a BMM150 three-axis magnetometer, a ULN2003 stepper driver, and a 28BYJ-48 stepper motor.

The BMM150 is connected to the ESP32 using I²C, typically through GPIO21 for SDA and GPIO22 for SCL. The magnetometer measures the local magnetic field, and the ESP32 converts the X and Y magnetic field readings into a heading from 0 to 360 degrees. Because magnetometers are sensitive to nearby metal, motor currents, and wiring, the code includes lightweight calibration logic that tracks minimum and maximum magnetic readings over time. This allows the system to estimate hard-iron offset and basic soft-iron scaling.

The resulting heading is compared to the target azimuth received from the gateway. If the difference between the current heading and the target heading is greater than the tolerance window, the ESP32 commands the stepper motor to rotate in the appropriate direction. Once the heading is close enough, the motor stops.

## Altitude Control Module

The altitude control module is responsible for tilting the telescope up and down. This module uses a second ESP32-WROOM board, an MPU6050 accelerometer/gyroscope module, a ULN2003 driver, and another 28BYJ-48 stepper motor.

The MPU6050 is connected over I²C and is used as an inclinometer by calculating pitch from the accelerometer values. This pitch represents the telescope’s current altitude angle. The ESP32 compares the measured pitch to the target altitude sent by the gateway. Unlike azimuth, altitude does not wrap around 360 degrees in the same way, so the altitude controller uses a simpler angle comparison. If the pitch differs from the target by more than the allowed tolerance, the motor moves in the direction that reduces the error. When the current altitude is within the deadband, the motor stops to avoid jitter.

## Motor Control

The 28BYJ-48 motors provide the mechanical movement for both axes. One motor is assigned to azimuth rotation and the other to altitude adjustment. Each motor is driven by its own ULN2003 driver board, which allows the low-power ESP32 GPIO pins to control the higher-current motor coils.

The control code uses the AccelStepper library to send step signals to the ULN2003 board in a non-blocking way. This is important because each ESP32 must continuously read its sensor, receive wireless target updates, and drive the motor without freezing the rest of the program. The motors are used for coarse positioning, with a tolerance of roughly ±5 degrees. This deadband prevents the motors from constantly twitching due to small sensor fluctuations.

## Power System

Power is separated between the logic electronics and the motor system to improve reliability. Each 28BYJ-48 motor is powered by an external 3.7 V battery source that is boosted to 5 V using an MT3608 boost converter. The boosted 5 V output powers the motor side of the ULN2003 driver. The ESP32 boards and sensors remain on their own regulated 3.3 V logic rails.

This separation is important because stepper motors can draw bursts of current and generate electrical noise that could otherwise cause the ESP32s to reset or corrupt sensor readings. Even though the power rails are separated, all grounds are tied together so that the ESP32s, sensors, driver boards, and Raspberry Pi share a common electrical reference. The MT3608 boost converters should be adjusted to output approximately 5 V under load, and they should be physically placed away from the BMM150 magnetometer because the inductor and switching current can distort magnetic readings.

## I²C, UART, SPI, and ESP-NOW Communication

I²C is used as the sensor communication bus on both axis modules. The altitude ESP32 uses I²C to communicate with the MPU6050, while the azimuth ESP32 uses I²C to communicate with the BMM150. I²C is well suited for this because it only requires two signal wires, SDA and SCL, and allows the ESP32 to read sensor data repeatedly during operation. The sensors operate at 3.3 V logic, so they can connect directly to the ESP32 without level shifting when using compatible breakout boards. The project uses a conservative I²C clock speed, such as 100 kHz, to improve reliability with jumper wires and breadboard-style connections. Pull-up resistors on SDA and SCL are required; many breakout boards include them, but bare modules may require external 4.7 kΩ pull-ups to 3.3 V.

UART-style serial communication is used between the Raspberry Pi and the gateway ESP32. The tracker program on the Pi sends the desired azimuth and altitude values to the gateway as plain text. The gateway reads serial input non-blockingly, parses the incoming line, and updates its stored target values. This simple text-based format makes debugging easier because commands can be viewed directly in the serial monitor or logs. UART is also used throughout development for debugging: each ESP32 can print its current readings, received targets, and status messages over USB serial.

SPI is not required in the current version of the project because both sensors are connected over I²C, and the wireless communication is handled by ESP-NOW. However, SPI remains available for future expansion. For example, an SPI display, SD card logger, or higher-speed sensor could be added later. If SPI is introduced, care would need to be taken to avoid pin conflicts with the motor driver pins and to keep SPI wiring short and away from motor power lines.

ESP-NOW is the wireless protocol that connects the gateway ESP32 to the two sensor/motor ESP32s. The gateway sends azimuth targets to the BMM150 node and altitude targets to the MPU6050 node. Each receiver updates its target value whenever a new packet arrives. The axis modules also send feedback back to the gateway, such as the current heading or angle, so the system can log what the hardware is actually doing. ESP-NOW is useful here because it does not require a traditional Wi-Fi network connection between all ESP32s, and it has low latency. All ESP32 modules are configured to use the same Wi-Fi channel so packets can be exchanged reliably.

## Closed-Loop Control Strategy

The closed-loop behavior of the system is intentionally simple. Each axis controller receives a target value, reads its sensor, calculates the difference between the target and current value, and decides whether the motor should move. If the error is greater than the threshold, the motor turns in the direction that reduces the error. If the error is within the threshold, the motor stops.

This approach is easier to tune than a full PID controller and is sufficient for demonstrating the concept of automated telescope pointing. In the future, the control algorithm could be improved by adding proportional speed control, acceleration profiles, or PID feedback to reduce overshoot and improve smoothness.

## System Integration

The final result is a modular telescope-pointing system where each subsystem has a clear responsibility. The Raspberry Pi handles the web interface and Skyfield-based astronomical calculations. The gateway handles message routing. The azimuth ESP32 handles compass-based heading control. The altitude ESP32 handles inclinometer-based elevation control. The motors provide physical movement, while the sensors provide feedback.

This distributed design makes the system easier to debug because each part can be tested independently before being integrated into the full tracker. The web interface can be tested separately from the embedded controllers, the gateway can be tested with serial commands before receiving live Skyfield data, and each axis module can be tested with manually entered target values before being mounted to the telescope.

## Future Improvements

Future improvements could include tilt-compensated magnetometer readings by combining BMM150 data with accelerometer pitch and roll, persistent calibration storage so the magnetometer does not need to relearn offsets on every boot, mechanical end stops for safety, a more precise motor driver, and a stronger telescope mounting bracket. The software side could also be expanded to support stars, constellations, satellites, or manually entered coordinates by taking further advantage of Skyfield’s ability to work with ephemerides, time scales, observer locations, and apparent sky coordinates.

Overall, the project demonstrates how web software, astronomical computation, embedded systems, wireless communication, sensors, and motor control can work together to create a functional automated telescope tracker.
