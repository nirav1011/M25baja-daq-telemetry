// BNO085 driver over SPI, talking SHTP/SH-2 directly (no vendor library).
//
// SHTP framing: every packet starts with a 4-byte header
//   [len_lsb][len_msb][channel][seq_num]
//   bit 15 of the length is a continuation flag.
//
// Breakout must be strapped for SPI mode (PS0 = PS1 = 1).

#include "sensors/imu_sensor.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "imu";

// SH-2 channels
#define CH_COMMAND          0
#define CH_EXECUTABLE       1
#define CH_CONTROL          2
#define CH_INPUT_REPORTS    3
#define CH_WAKE_REPORTS     4
#define CH_GYRO_INTEGRATED  5

// Sensor report IDs (SH-2 reference manual section 6.5)
#define REPORT_ACCELEROMETER     0x01
#define REPORT_GYROSCOPE         0x02
#define REPORT_ROTATION_VECTOR   0x05
#define REPORT_TIMESTAMP_REBASE  0xFA
#define REPORT_BASE_TIMESTAMP    0xFB

// Q-point scaling (fixed-point exponents from datasheet)
#define ACCEL_Q_POINT     8
#define GYRO_Q_POINT      9
#define ROTATION_Q_POINT  14

// SH-2 commands
#define SET_FEATURE_COMMAND  0xFD

IMUSensor::IMUSensor(uint8_t mosi_pin_, uint8_t miso_pin_, uint8_t sck_pin_,
                     uint8_t cs_pin_, uint8_t int_pin_, uint8_t rst_pin_)
    : mosi_pin(mosi_pin_), miso_pin(miso_pin_), sck_pin(sck_pin_),
      cs_pin(cs_pin_), int_pin(int_pin_), rst_pin(rst_pin_),
      yaw_deg(0), pitch_deg(0), roll_deg(0),
      accel_x(0), accel_y(0), accel_z(0),
      gyro_x(0), gyro_y(0), gyro_z(0),
      qw(1), qi(0), qj(0), qk(0),
      ready(false)
{
    memset(seq_nums, 0, sizeof(seq_nums));
    // constructor does NOTHING that touches FreeRTOS or hardware
}

bool IMUSensor::init() {
    gpio_config_t out_conf = {};
    out_conf.pin_bit_mask = (1ULL << cs_pin) | (1ULL << rst_pin);
    out_conf.mode         = GPIO_MODE_OUTPUT;
    gpio_config(&out_conf);

    gpio_config_t in_conf = {};
    in_conf.pin_bit_mask = (1ULL << int_pin);
    in_conf.mode         = GPIO_MODE_INPUT;
    in_conf.pull_up_en   = GPIO_PULLUP_ENABLE;   // INT is active low
    gpio_config(&in_conf);

    cs_high();

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num     = mosi_pin;
    bus_config.miso_io_num     = miso_pin;
    bus_config.sclk_io_num     = sck_pin;
    bus_config.quadwp_io_num   = -1;
    bus_config.quadhd_io_num   = -1;
    bus_config.max_transfer_sz = IMU_PACKET_SIZE;
    if (spi_bus_initialize(IMU_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return false;
    }

    spi_device_interface_config_t dev_config = {};
    dev_config.mode           = 3;
    dev_config.clock_speed_hz = IMU_SPI_FREQ_HZ;
    dev_config.spics_io_num   = -1;   // CS driven manually so header + body share one assertion
    dev_config.queue_size     = 1;
    if (spi_bus_add_device(IMU_SPI_HOST, &dev_config, &spi_handle) != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed");
        return false;
    }

    if (!hardware_reset()) {
        ESP_LOGE(TAG, "no response after reset (check wiring / PS0+PS1 straps)");
        return false;
    }

    bool ok = true;
    ok &= enable_report(REPORT_ROTATION_VECTOR, 10000);   // 100 Hz
    ok &= enable_report(REPORT_ACCELEROMETER,   10000);
    ok &= enable_report(REPORT_GYROSCOPE,       10000);
    if (!ok) {
        ESP_LOGE(TAG, "failed to enable reports");
        return false;
    }

    ready = true;
    ESP_LOGI(TAG, "IMU OK");
    return true;
}

// pulse RST low to fully reset, then wait for the sensor to pull INT low
// with its startup advertisement.
//
// The advertisement is deliberately NOT drained here: without the WAKE pin
// the host may only write while INT is asserted, and once the startup packets
// are read INT goes idle until reports are enabled. The first Set Feature
// write goes out while the advertisement is pending; each write then triggers
// a Get Feature Response, which re-asserts INT for the next one.
bool IMUSensor::hardware_reset() {
    gpio_set_level((gpio_num_t)rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));   // let INT settle before trusting it

    return wait_for_int(1000);       // datasheet says ~200 ms boot
}

// BNO085 pulls INT low when it has data or is ready to accept a write
bool IMUSensor::wait_for_int(uint32_t timeout_ms) {
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) / 1000 < timeout_ms) {
        if (gpio_get_level((gpio_num_t)int_pin) == 0) return true;
        vTaskDelay(1);
    }
    return false;
}

// SH-2 packet header: [len_lsb][len_msb][channel][seq_num], then payload
bool IMUSensor::send_packet(uint8_t channel, const uint8_t* data, uint16_t length) {
    uint16_t total_len = length + 4;
    if (total_len > IMU_PACKET_SIZE) return false;

    tx_buffer[0] = total_len & 0xFF;
    tx_buffer[1] = (total_len >> 8) & 0x7F;   // top bit reserved
    tx_buffer[2] = channel;
    tx_buffer[3] = seq_nums[channel]++;
    memcpy(&tx_buffer[4], data, length);

    if (!wait_for_int(200)) return false;

    // SPI is full duplex: whatever the sensor clocks out during our write is
    // discarded here; if it was a partial packet the rest arrives as a continuation.
    spi_transaction_t trans = {};
    trans.length    = total_len * 8;
    trans.tx_buffer = tx_buffer;
    trans.rx_buffer = rx_buffer;

    cs_low();
    esp_err_t ret = spi_device_polling_transmit(spi_handle, &trans);
    cs_high();

    return ret == ESP_OK;
}

// reads a single SH-2 packet; reads header first to learn length, then body
bool IMUSensor::receive_packet() {
    if (gpio_get_level((gpio_num_t)int_pin) != 0) return false;

    // read 4 byte header (tx of all zeros = zero-length write, which the sensor ignores)
    memset(tx_buffer, 0, IMU_PACKET_SIZE);
    spi_transaction_t header = {};
    header.length    = 4 * 8;
    header.tx_buffer = tx_buffer;
    header.rx_buffer = rx_buffer;

    cs_low();
    if (spi_device_polling_transmit(spi_handle, &header) != ESP_OK) {
        cs_high();
        return false;
    }

    uint16_t packet_len = ((rx_buffer[1] & 0x7F) << 8) | rx_buffer[0];
    uint8_t  channel    = rx_buffer[2];

    // packet_len of 0 = nothing to send; otherwise must fit in our buffer
    if (packet_len < 4 || packet_len > IMU_PACKET_SIZE) {
        cs_high();
        return false;
    }

    // read remaining payload under the same CS assertion
    if (packet_len > 4) {
        spi_transaction_t body = {};
        body.length    = (packet_len - 4) * 8;
        body.tx_buffer = &tx_buffer[4];
        body.rx_buffer = &rx_buffer[4];
        if (spi_device_polling_transmit(spi_handle, &body) != ESP_OK) {
            cs_high();
            return false;
        }
    }
    cs_high();

    if (channel == CH_INPUT_REPORTS) {
        parse_input_reports(packet_len);
    }
    return true;
}

// Input-report packets carry a 5 byte base-timestamp record followed by one or
// more sensor reports back to back (the sensor batches reports that are ready
// at the same time), so walk the whole payload instead of reading just the first.
void IMUSensor::parse_input_reports(uint16_t packet_len) {
    auto read_int16 = [](const uint8_t* p) -> int16_t {
        return (int16_t)(p[0] | (p[1] << 8));
    };

    uint16_t i = 4;   // skip SHTP header
    while (i < packet_len) {
        const uint8_t* payload = &rx_buffer[i];
        uint8_t report_id = payload[0];
        uint16_t report_len;

        switch (report_id) {
            case REPORT_BASE_TIMESTAMP:
            case REPORT_TIMESTAMP_REBASE: report_len = 5;  break;
            case REPORT_ACCELEROMETER:
            case REPORT_GYROSCOPE:        report_len = 10; break;
            case REPORT_ROTATION_VECTOR:  report_len = 14; break;
            default: return;   // unknown length -> can't safely continue
        }
        if (i + report_len > packet_len) return;

        // bytes 0..3 are report id / seq / status / delay; data starts at offset 4
        if (report_id == REPORT_ROTATION_VECTOR) {
            float scale = 1.0f / (1 << ROTATION_Q_POINT);
            qi = read_int16(&payload[4])  * scale;
            qj = read_int16(&payload[6])  * scale;
            qk = read_int16(&payload[8])  * scale;
            qw = read_int16(&payload[10]) * scale;
            quaternion_to_euler();
        }
        else if (report_id == REPORT_ACCELEROMETER) {
            float scale = 1.0f / (1 << ACCEL_Q_POINT);
            accel_x = read_int16(&payload[4]) * scale;
            accel_y = read_int16(&payload[6]) * scale;
            accel_z = read_int16(&payload[8]) * scale;
        }
        else if (report_id == REPORT_GYROSCOPE) {
            float scale = 1.0f / (1 << GYRO_Q_POINT);
            gyro_x = read_int16(&payload[4]) * scale;
            gyro_y = read_int16(&payload[6]) * scale;
            gyro_z = read_int16(&payload[8]) * scale;
        }

        i += report_len;
    }
}

// converts unit quaternion to yaw/pitch/roll in degrees (ZYX intrinsic)
void IMUSensor::quaternion_to_euler() {
    // roll (x-axis rotation)
    float sinr_cosp = 2.0f * (qw * qi + qj * qk);
    float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
    roll_deg = atan2f(sinr_cosp, cosr_cosp) * 180.0f / (float)M_PI;

    // pitch (y-axis rotation); clamp at +/- 90 to avoid gimbal lock NaNs
    float sinp = 2.0f * (qw * qj - qk * qi);
    if (fabsf(sinp) >= 1.0f)
        pitch_deg = copysignf((float)M_PI / 2.0f, sinp) * 180.0f / (float)M_PI;
    else
        pitch_deg = asinf(sinp) * 180.0f / (float)M_PI;

    // yaw (z-axis rotation)
    float siny_cosp = 2.0f * (qw * qk + qi * qj);
    float cosy_cosp = 1.0f - 2.0f * (qj * qj + qk * qk);
    yaw_deg = atan2f(siny_cosp, cosy_cosp) * 180.0f / (float)M_PI;
}

// SH-2 "Set Feature Command" tells the sensor to start producing a given report
// at the requested interval (microseconds, little-endian)
bool IMUSensor::enable_report(uint8_t report_id, uint32_t interval_us) {
    uint8_t cmd[17] = {0};
    cmd[0] = SET_FEATURE_COMMAND;
    cmd[1] = report_id;
    cmd[5] = interval_us         & 0xFF;
    cmd[6] = (interval_us >> 8)  & 0xFF;
    cmd[7] = (interval_us >> 16) & 0xFF;
    cmd[8] = (interval_us >> 24) & 0xFF;

    if (!send_packet(CH_CONTROL, cmd, sizeof(cmd))) {
        ESP_LOGW(TAG, "enable report 0x%02X failed", report_id);
        return false;
    }
    return true;
}

// drain everything the sensor has queued up since last call
void IMUSensor::update() {
    if (!ready) return;

    while (gpio_get_level((gpio_num_t)int_pin) == 0) {
        if (!receive_packet()) break;
    }
}
