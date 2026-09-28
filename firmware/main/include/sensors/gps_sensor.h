#ifndef GPS_SENSOR_H
#define GPS_SENSOR_H

#include <cmath>
#include <cstdint>
 
namespace gps_sensor {
 
struct Data {
  bool     fix         = false;
  uint8_t  satellites  = 0; 
  uint8_t  fixQuality  = 0;
 
  double   latitudeDeg  = NAN;   // signed: + N / − S
  double   longitudeDeg = NAN;   // signed: + E / − W
  float    altitudeM    = NAN;
  float    speedKnots   = NAN;
  float    courseDeg    = NAN;   // true course over ground
 
  uint16_t year = 0;
  uint8_t  month = 0, day = 0;
  uint8_t  hour = 0, minute = 0, second = 0;
};
 
// One-time setup. Returns true on success.
bool init();
 
// Pumps UART → NMEA parser. Call frequently from the main loop.
// Returns true if Data was updated this call.
bool update();
 
// Latest cached fix.
const Data& get();
 
// Convenience: prints to stdout.
void print();
 
}  // namespace gps_sensor

#endif
