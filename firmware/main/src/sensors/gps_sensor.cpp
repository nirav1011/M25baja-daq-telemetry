// Adafruit Ultimate GPS v3 over UART — ESP32-S3, ESP-IDF, C++.
//
// Wiring (pins in pins.h):
//   GPS TX → ESP32-S3 GPIO 44 (RX)
//   GPS RX → ESP32-S3 GPIO 43 (TX)   ← optional; default GPS cadence is fine
//   GPS VIN → 3.3V or 5V
//   GPS GND → GND

#include "sensors/gps_sensor.h"
#include "pins.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "driver/uart.h"
#include "esp_log.h"

namespace gps_sensor {

namespace {

constexpr const char *TAG = "gps";

constexpr uart_port_t UART_NUM = UART_NUM_2;
constexpr int  BAUD     = 9600;
constexpr int  RX_BUF   = 1024;
constexpr int  LINE_BUF = 128;

Data    s_data;
char    s_line[LINE_BUF];
size_t  s_pos = 0;

bool checksum_ok(const char *line) {
  if (line[0] != '$')
    return false;
  const char *star = std::strchr(line, '*');
  if (!star || std::strlen(star) < 3)
    return false;
  uint8_t sum = 0;
  for (const char *p = line + 1; p < star; ++p)
    sum ^= static_cast<uint8_t>(*p);
  uint8_t want = static_cast<uint8_t>(std::strtol(star + 1, nullptr, 16));
  return sum == want;
}

double parse_coord(const char *deg_min, char hem) { //split degrees and minutes
  if (!deg_min || !*deg_min) return NAN;
  double v = std::atof(deg_min);
  double deg = static_cast<int>(v / 100);
  double min = v - deg * 100;
  double out = deg + min / 60.0;
  if (hem == 'S' || hem == 'W') out = -out;
  return out;
}

int split(char *line, char *argv[], int max) { //gps string to array of strings
  int n = 0;
  argv[n++] = line;
  for (char *p = line; *p && n < max; ++p) {
    if (*p == ',' || *p == '*') { *p = '\0'; argv[n++] = p + 1; }
  }
  return n;
}

void parse_gga(char *line) { //extract latitude and longitude
  // $GxGGA, time, lat, N/S, lon, E/W, fixQ, sats, HDOP, alt, M, ...
  char *f[16] = {};
  if (split(line, f, 16) < 11) return;
  s_data.fixQuality = static_cast<uint8_t>(std::atoi(f[6]));
  s_data.satellites = static_cast<uint8_t>(std::atoi(f[7]));
  s_data.fix        = (s_data.fixQuality > 0);
  if (s_data.fix) {
    s_data.latitudeDeg  = parse_coord(f[2], f[3][0]);
    s_data.longitudeDeg = parse_coord(f[4], f[5][0]);
    //add speed?
    s_data.altitudeM    = static_cast<float>(std::atof(f[9]));
  }
}

void parse_rmc(char *line) { //extract time and motion
  // $GxRMC, time, A/V, lat, N/S, lon, E/W, speedKn, course, ddmmyy, ...
  char *f[16] = {};
  if (split(line, f, 16) < 10) return;
  if (f[1] && std::strlen(f[1]) >= 6) {
    s_data.hour   = (f[1][0]-'0')*10 + (f[1][1]-'0');
    s_data.minute = (f[1][2]-'0')*10 + (f[1][3]-'0');
    s_data.second = (f[1][4]-'0')*10 + (f[1][5]-'0');
  }
  if (f[9] && std::strlen(f[9]) >= 6) {
    s_data.day   = (f[9][0]-'0')*10 + (f[9][1]-'0');
    s_data.month = (f[9][2]-'0')*10 + (f[9][3]-'0');
    s_data.year  = 2000 + (f[9][4]-'0')*10 + (f[9][5]-'0');
  }
  s_data.speedKnots = static_cast<float>(std::atof(f[7])); // multiply by 0.5144 for m/s
  if (*f[8]) s_data.courseDeg = static_cast<float>(std::atof(f[8])); // blank when stationary
}

bool process_line(char *line) { //uses checksum to verify
  if (!checksum_ok(line)) return false;
  // Talker-id agnostic: $G_GGA / $G_RMC where _ is P/N/L/A/B
  if      (std::strstr(line, "GGA,") == line + 3) { parse_gga(line); return true; }
  else if (std::strstr(line, "RMC,") == line + 3) { parse_rmc(line); return true; }
  return false;
}

}  // anonymous namespace

bool init() { //initializes uart
  uart_config_t cfg = {};
  cfg.baud_rate  = BAUD;
  cfg.data_bits  = UART_DATA_8_BITS;
  cfg.parity     = UART_PARITY_DISABLE;
  cfg.stop_bits  = UART_STOP_BITS_1;
  cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  if (uart_driver_install(UART_NUM, RX_BUF, 0, 0, nullptr, 0) != ESP_OK) return false;
  if (uart_param_config(UART_NUM, &cfg) != ESP_OK) return false;
  if (uart_set_pin(UART_NUM, GPS_TX_PIN, GPS_RX_PIN,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;

  // Optional config — module's default is already RMC+GGA at 1 Hz, so even if
  // you don't connect ESP32 TX to GPS RX physically, you'll still get fixes.
  const char *cmd1 = "$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n";
  const char *cmd2 = "$PMTK220,1000*1F\r\n";
  uart_write_bytes(UART_NUM, cmd1, std::strlen(cmd1));
  uart_write_bytes(UART_NUM, cmd2, std::strlen(cmd2));

  ESP_LOGI(TAG, "GPS OK");
  return true;
}

bool update() { //keep updating
  uint8_t b;
  bool updated = false;
  while (uart_read_bytes(UART_NUM, &b, 1, 0) > 0) {
    if (b == '\n' || b == '\r') {
      if (s_pos > 0) {
        s_line[s_pos] = '\0';
        if (process_line(s_line)) updated = true;
        s_pos = 0;
      }
    } else if (s_pos < LINE_BUF - 1) {
      s_line[s_pos++] = static_cast<char>(b);
    } else {
      s_pos = 0;   // overflow → discard
    }
  }
  return updated;
}

const Data& get() { return s_data; }

void print() {
  if (s_data.fix) {
    std::printf("[GPS] Fix: YES  Sats: %d  Quality: %d\n",
                s_data.satellites, s_data.fixQuality);
    std::printf("[GPS] Lat: %.6f  Lon: %.6f\n",
                s_data.latitudeDeg, s_data.longitudeDeg);
    std::printf("[GPS] Speed: %.2f kn  Course: %.1f deg  Alt: %.2f m\n",
                s_data.speedKnots, s_data.courseDeg, s_data.altitudeM);
    std::printf("[GPS] %04d-%02d-%02d  %02d:%02d:%02d UTC\n",
                s_data.year, s_data.month, s_data.day,
                s_data.hour, s_data.minute, s_data.second);
  } else {
    std::printf("[GPS] No fix yet — waiting for satellites...\n");
  }
}

}  // namespace gps_sensor
