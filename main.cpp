#include <unistd.h>
#include <strings.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <assert.h>

#include "opi_linux.h"
#include "jack_client.h"

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

int init_openucd_and_module(HANDLE &comprt)
{
    OPIPKT_t onepkt;
    int rc = opi_openucd_com(&comprt);;
    if (rc != 0)
        return 1;
    if (opiucd_onmode(&comprt) != 0)
        return 1;
    rc = opiucd_status(&comprt, &onepkt);
    if (rc == 0)
        opipkt_dump(&onepkt);
    else
        return 2;
    rc = opiucd_tsstatus(&comprt, &onepkt);
    if (rc == 0)
    {
        opipkt_dump(&onepkt);
        printout_ts_status(onepkt);
    }
    else
    {
        fprintf(stderr,"Plug in Sensor pls.\n");
        return 3;
    }
    if (opiucd_settszbchan(&comprt, 13) != 0)
        return 4;
    if (opiucd_setzbchan(&comprt, 13) != 0)
        return 5;
    if (opiucd_settsrfmode(&comprt, 0x01) != 0) // 0x01 - RF on and double tap&timeout off
        return 6;
    if (opiucd_settsmmwrite(&comprt, false) != 0) // disable MM write
        return 7;
    if (opiucd_copytssettings(&comprt, 0) != 0) // 0x01 - RF on and double tap&timeout off
        return 20;
    return 0;
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
        printf("ADC Sample %d: %d\n", adcc, ((int16_t) opipkt.payload[pb++]) << 8 + ((int16_t) opipkt.payload[pb++] & 0xFC) );
    }
    //printf("pb: %d ",pb);
    assert(pb==137 || pb ==133);
    printf("Temp: %f degree C\n", ((double) opipkt.payload[pb++]) * 1.13 - 46.8);
    printf("Accel: %d %d %d\n", (int8_t) opipkt.payload[pb++], (int8_t) opipkt.payload[pb++], ((int32_t) opipkt.payload[pb++]) << 24 + ((int32_t) opipkt.payload[pb++]) << 16 + ((int32_t) opipkt.payload[pb++]) << 8 + ((int32_t) opipkt.payload[pb++]) );
    printf("ED: %d\n", ntohs(opipkt.payload[pb++]));
}

void copy_data_to_jack(OPIPKT_t &opipkt)
{
    unsigned int pb = 8;
    for (unsigned int adcc = 0; adcc < (opipkt.length - 17) / 2; adcc++)
    {
        jack_append_new_data( ((int16_t) opipkt.payload[pb++]) << 8 + ((int16_t) opipkt.payload[pb++] & 0xFC) );
    }

    //let's not do crc, we don't really need correct data anyway ... it's art ;->
}

int main(int argc, char* argv[])
{
    HANDLE comprt;
    OPIPKT_t onepkt;

    jack_init();

    int rc = init_openucd_and_module(comprt);
    if (rc != 0)
    {
        opi_closeucd_com(&comprt);
        return rc;
    }

    jack_run();
    while (1)
    {
        //rc = opiucd_status(&comprt, &onepkt);
        bzero(&onepkt, sizeof(OPIPKT_t));
        rc = opiucd_getwltsdata(&comprt, &onepkt);
        if (rc == 1)
        {
            opipkt_dump(&onepkt);
            interpret_data_packet(onepkt);
            copy_data_to_jack(onepkt);
        }
        else if (rc == 0)
        {
            //printf("got OK/NoData\n");
        }
        else
            break;
    }
    jack_byebye();
    opi_closeucd_com(&comprt);
    return rc;
}
