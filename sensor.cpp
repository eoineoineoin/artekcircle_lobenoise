#include "sensor.h"
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>

// Most code here is by Bernhard Tittelbach, I just refactored it so that it
// is easier to use it in GUIs etc.

SensorError init_openucd_and_module(HANDLE &comprt)
{
    OPIPKT_t onepkt;
    int rc = opi_openucd_com(&comprt);
    if (rc != 0)
    {
        fprintf(stderr, "could not find ucd device\n");
        return ERR_NODEVICE;
    }
    if (opiucd_onmode(&comprt) != 0)
        return ERR_NODEVICE;
    rc = opiucd_status(&comprt, &onepkt);
    if (rc == 0)
        opipkt_dump(&onepkt);
    else
        return ERR_INITSTATUS;
    return ERR_OK;
}

// ugly code for beautiful art :-)

void printout_ts_status(OPIPKT_t &opipkt)
{
    int64_t moddsn, modrtc, modrtcSet, modRefEpochMSecs;
    int32_t modfwv;
    int16_t modpdn, modzbChan, modRFMode, modRFTxPwr, modMMWrite, modRFTxTimeout;

    moddsn = ((int64_t) opipkt.payload[1] << 32) + ((int64_t)opipkt.payload[2] << 24)
            + (opipkt.payload[3] << 16) + (opipkt.payload[4] << 8) + opipkt.payload[5];
    modrtc = ((int64_t) opipkt.payload[1+DSNLEN] << 32) + ((int64_t) opipkt.payload[1+DSNLEN+1] << 24)
            + (opipkt.payload[1+DSNLEN+2] << 16) + (opipkt.payload[1+DSNLEN+3] << 8) + opipkt.payload[1+DSNLEN+4];
    modrtcSet = modrtc >> 39;
    if (modrtcSet) modRefEpochMSecs = (modrtc - (((int64_t) 1) << 39))*64*1000/32768;
    else modRefEpochMSecs = modrtc*64*1000/32768;

    modfwv = (opipkt.payload[1+DSNLEN+5] << 8) + opipkt.payload[1+DSNLEN+5+1];
    modpdn = opipkt.payload[1+DSNLEN+5+FWVLEN];
    modzbChan = opipkt.payload[1+DSNLEN+5+FWVLEN+1];
    modRFMode = opipkt.payload[1+DSNLEN+5+FWVLEN+2];
    modRFTxPwr = opipkt.payload[1+DSNLEN+5+FWVLEN+3];
    modMMWrite = opipkt.payload[1+DSNLEN+5+FWVLEN+4];
    modRFTxTimeout = opipkt.payload[1+DSNLEN+5+FWVLEN+5];

    printf("TrueSense Status\n");
    printf("Paired Device Number: %d   Firmware Version: %d  ZigBee Channel: %d\n", modpdn, modfwv, modzbChan);
    if(modRFMode == 0)
        printf("RF Always Off\n");
    else if(modRFMode == 1)
        printf("RF Always On\n");
    else if(modRFMode == 2 || modRFMode == 3)
    {
        if(modRFTxTimeout == 0)
            printf("RF On with double tap enabled\n");
        else if(modRFTxTimeout == 1)
            printf("RF On with double tap enabled and timeout of 30 min\n");
        else if(modRFTxTimeout == 2)
            printf("RF On with double tap enabled and timeout of 1 hour\n");
    }
    if(modMMWrite)
        printf("Memory Module Write Enabled\n");
    else
        printf("Memory Module Write Disabled\n");
}

const char *get_sensor_error_string(SensorError error)
{
    switch(error)
    {
        case ERR_NODEVICE: return "Controller not connected or not accessible";
        case ERR_INITSTATUS: return "Cannot read the initial status from the controller";
        case ERR_NOSENSOR: return "No sensor connected. Please plug in the sensor.";
        case ERR_SENSORZBCONF: return "Cannot set sensor's ZigBee channel.";
        case ERR_CTRLZBCONF: return "Cannot set controller's ZigBee channel.";
        case ERR_RFMODE: return "Cannot set sensor's RF mode.";
        case ERR_MMWRITE: return "Cannot disable memory module access.";
        case ERR_COPYSETTINGS: return "Cannot copy settings to controller slot.";
    }
}

SensorError configure_sensor(HANDLE &comprt)
{
    OPIPKT_t onepkt;
    int rc = opiucd_tsstatus(&comprt, &onepkt);
    if (rc == 0)
    {
        opipkt_dump(&onepkt);
        printout_ts_status(onepkt);
    }
    else
    {
        fprintf(stderr,"Plug in Sensor pls.\n");
        return ERR_NOSENSOR;
    }
    if (opiucd_settszbchan(&comprt, 13) != 0)
        return ERR_SENSORZBCONF;
    if (opiucd_setzbchan(&comprt, 13) != 0)
        return ERR_CTRLZBCONF;
    if (opiucd_settsrfmode(&comprt, 0x01) != 0) // 0x01 - RF on and double tap&timeout off
        return ERR_RFMODE;
    if (opiucd_settsmmwrite(&comprt, false) != 0) // disable MM write
        return ERR_MMWRITE;
    if (opiucd_copytssettings(&comprt, 0) != 0) // copy settings to slot 0
        return ERR_COPYSETTINGS;
    return ERR_OK;
}

/**
 *  Timestamp (6);
 * Wireless Frame PDN (1);
 * Wireless Frame Misc (1);
 * ADC Channel 0 64/62 13.5b samples (128/124);
 * Temperature Code (1);
 * Accelerometer (6);
 * ED Measurement (1);
**/
void interpret_data_packet(OPIPKT_t &opipkt)
{
    unsigned int pb=0;
    unsigned int so=6;
    if (opipkt.dataCode != 0x01)
        return;
    if (opipkt.payload[pb++] != 0x01)
        return;
    printf("Length: %d\n", opipkt.length);
    //printf("pb: %d ",pb);
    printf("TS: %d %d %d %d %d %d\n", opipkt.payload[pb++], opipkt.payload[pb++], opipkt.payload[pb++], opipkt.payload[pb++], opipkt.payload[pb++], opipkt.payload[pb++]);
    //printf("pb: %d ",pb);
    printf("Frame PDN: %d\n", opipkt.payload[pb++]);
    //printf("pb: %d ",pb);
    printf("Frame Misc: %d, long adc data: %d, battery ok: %d\n", opipkt.payload[pb], opipkt.payload[pb] & 0x80, opipkt.payload[pb] & 0x01);
    pb++;
    printf("pb: %d ",pb);
    printf("ADC Samples Data Len: %d\n", opipkt.length - 17);
    for (unsigned int adcc = 0; adcc < (opipkt.length - 17) / 2; adcc++)
    {
        int16_t v = (((int16_t) opipkt.payload[pb++]) << 8) + (((int16_t) opipkt.payload[pb++] & 0xFC));
        printf("ADC Sample %d: %d\n", adcc, v );
    }
    //printf("pb: %d ",pb);
    assert(pb==137 || pb ==133);
    printf("Temp: %f degree C\n", ((double) opipkt.payload[pb++]) * 1.13 - 46.8);
    printf("Accel: %d %d %d\n", (int8_t) opipkt.payload[pb++], (int8_t) opipkt.payload[pb++], ((int32_t) opipkt.payload[pb++]) << 24 + ((int32_t) opipkt.payload[pb++]) << 16 + ((int32_t) opipkt.payload[pb++]) << 8 + ((int32_t) opipkt.payload[pb++]) );
    printf("ED: %d\n", ntohs(opipkt.payload[pb++]));
}

/**
 *  Timestamp (6);
 * Wireless Frame PDN (1);
 * Wireless Frame Misc (1);
 * ADC Channel 0 64/62 13.5b samples (128/124);
 * Temperature Code (1);
 * Accelerometer (6);
 * ED Measurement (1);
**/
bool data_packet_to_sdp(OPIPKT_t &opipkt, SensorDataPacket &sdp)
{
    unsigned int pb=0;
    unsigned int so=6;
    if (opipkt.dataCode != 0x01)
        return false;
    if (opipkt.payload[pb++] != 0x01)
        return false;
    sdp.length = opipkt.length;
    memcpy(&sdp.timestamp_bytes, &opipkt.payload[pb], 6);
    pb += 6;
    sdp.frame_pdn = opipkt.payload[pb++];
    sdp.flags = opipkt.payload[pb++];
    printf("ADC Samples Data Len: %d\n", opipkt.length - 17);
    unsigned int adcc;
    for (adcc = 0; adcc < (opipkt.length - 17) / 2; adcc++)
        sdp.data[adcc] = (((int16_t) opipkt.payload[pb++]) << 8) + (((int16_t) opipkt.payload[pb++] & 0xFC));
    sdp.data_count = adcc;
    //printf("pb: %d ",pb);
    assert(pb==137 || pb ==133);
    sdp.tempC = ((double) opipkt.payload[pb++]) * 1.13 - 46.8;
    sdp.accelX = (int8_t) opipkt.payload[pb++];
    sdp.accelY = (int8_t) opipkt.payload[pb++];
    memcpy(sdp.accelU, &opipkt.payload[pb], 4);
    pb += 4;
    sdp.ed = opipkt.payload[pb++];
    return true;
}

