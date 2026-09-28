#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "pins.h"
#include "sensors/imu_sensor.h"
#include "sensors/gps_sensor.h"

constexpr int64_t PRINT_INTERVAL_US = 500'000;   // 500 ms

static IMUSensor imu(IMU_MOSI_PIN, IMU_MISO_PIN, IMU_SCK_PIN,
                     IMU_CS_PIN, IMU_INT_PIN, IMU_RST_PIN);

extern "C" void app_main(void) {
  std::printf("\n=== ESP32-S3 | BNO085 (SPI) + GPS ===\n\n");

  if (!imu.init()) std::printf("[IMU] init failed — continuing with GPS only\n");
  if (!gps_sensor::init()) std::printf("[GPS] init failed\n");

  int64_t last_print = 0;
  for (;;) {
    imu.update();
    gps_sensor::update();

    int64_t now = esp_timer_get_time();
    if (now - last_print >= PRINT_INTERVAL_US) {
      last_print = now;
      if (imu.is_ready()) {
        std::printf("[IMU] Yaw: %7.2f  Pitch: %7.2f  Roll: %7.2f  (deg)\n",
                    imu.yaw(), imu.pitch(), imu.roll());
        std::printf("[IMU] Accel  X: %6.3f  Y: %6.3f  Z: %6.3f  m/s^2\n",
                    imu.ax(), imu.ay(), imu.az());
        std::printf("[IMU] Gyro   X: %6.3f  Y: %6.3f  Z: %6.3f  rad/s\n",
                    imu.gx(), imu.gy(), imu.gz());
      }
      gps_sensor::print();
      std::printf("--------------------------------------------\n");
    }

    vTaskDelay(1);   // yield to other tasks / watchdog
  }
}
