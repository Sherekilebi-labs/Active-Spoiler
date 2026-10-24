
# Wiring

## ESP32 devkit module N1 pinout (Servo drives)

### Important note:
I dont know which motor pin drives motors CW and CCW so keep the original control logic in spoiler servo drive for all servos that identify automatically which direction is correct for the pid loop.

I2C bus N1: SCL - gpio 21, SDA - gpio 22
I2C bus N2: SCL - gpio 33, SDA - gpio 32


### Motor 1:

H-bridge pwms:
pwm1 -- gpio 13
pwm2 -- gpio 12

MT6701 module:
I2C bus N1, on address 0x06

### Motor 2:

H-bridge pwms:
pwm1 -- gpio 14
pwm2 -- gpio 27

MT6701 module:
I2C bus N1 on address 0x46


### Motor 3:

H-bridge pwms:
pwm1 -- gpio 26
pwm2 -- gpio 25

MT6701 module:
I2C bus N2 on address 0x06

### Motor 4:

H-bridge pwms:
pwm1 -- gpio 5
pwm2 -- gpio 18

MT6701 module:
I2C bus N2 on address 0x46


## ESP32 devkit module N2 pinout (Gyro, website, control)

MPU6050: connected via default I2C bus

SCL -> gpio 22

SDA -> gpio 21


## communication between ESP32s:

ESP32s devkits are connected via 2nd UART pins labeled TX2 an RX2
