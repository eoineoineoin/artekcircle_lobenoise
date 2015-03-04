#include "opi_linux.h"
#include <stdint.h>

enum SensorError {
    ERR_OK = 0,
    ERR_NODEVICE = 1,
    ERR_INITSTATUS = 2,
    ERR_NOSENSOR = 3,
    ERR_SENSORZBCONF = 4,
    ERR_CTRLZBCONF = 5,
    ERR_RFMODE = 6,
    ERR_MMWRITE = 7,
    ERR_COPYSETTINGS = 20,
};

struct SensorDataPacket
{
    uint16_t length;
    uint8_t timestamp_bytes[6];
    uint8_t frame_pdn;
    uint8_t flags;
    int16_t data[64];
    uint16_t data_count;
    float tempC;
    int8_t accelX, accelY;
    uint8_t accelU[4];
    uint8_t ed;
    
    bool isLongData() const { return 0 != (flags & 0x80); }
    bool isBatteryOK() const { return 0 != (flags & 0x01); }
};

struct SensorConfig
{
    int zbchannel;
};

extern SensorConfig sensor_config;
const char *get_sensor_error_string(SensorError error);
SensorError configure_sensor(HANDLE &comprt);
SensorError init_openucd_and_module(HANDLE &comprt);
bool data_packet_to_sdp(OPIPKT_t &opipkt, SensorDataPacket &sdp);
void interpret_data_packet(OPIPKT_t &opipkt);
void load_sensor_config();
