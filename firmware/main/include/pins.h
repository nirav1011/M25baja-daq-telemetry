#ifndef PINS_H
#define PINS_H

// ESP32-S3 pin map

// BNO085 IMU (SPI) — breakout label in comments
constexpr int IMU_MOSI_PIN = 42;  // DI
constexpr int IMU_MISO_PIN = 9;   // SDA
constexpr int IMU_SCK_PIN  = 10;  // SCL
constexpr int IMU_CS_PIN   = 41;  // CS
constexpr int IMU_INT_PIN  = 39;  // INT
constexpr int IMU_RST_PIN  = 47;  // RST

// Adafruit Ultimate GPS v3 (UART)
constexpr int GPS_TX_PIN = 43;    // ESP32 TX -> GPS RX
constexpr int GPS_RX_PIN = 44;    // ESP32 RX <- GPS TX

#endif // PINS_H
