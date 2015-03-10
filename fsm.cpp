#include "fsm.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

using namespace std;

SensorStateThread::SensorStateThread()
{
    last_error = ERR_OK;
    state = SST_INITIALISING;
    stopping = false;
    inf = NULL;
    inf_recording_pos = 0;
    playback_frame = 0;
}

string SensorStateThread::get_recording_path() const
{
    char *tmp = g_build_filename(g_get_home_dir(), "brainwave-recordings", NULL);
    string fn = tmp;
    g_free(tmp);
    return fn;
}

bool SensorStateThread::set_file_input(const char *label)
{
    inf = fopen(get_recording_path().c_str(), "rb");
    if (!inf)
        return false;
    while(true)
    {
        char buf[1024];
        if (!fgets(buf, 1024, inf))
            break;
        if (buf[0] == '#')
        {
            const char *labelpos = strchr(buf + 1, ' ');
            if (!labelpos)
                continue;
            while(*labelpos == ' ')
                labelpos++;
            char *eolpos = (char *)strchr(labelpos, '\n');
            if (eolpos)
                *eolpos = '\0';
            printf("Label %s\n", labelpos);
            // Just in case someone saves it with CRLF endings
            if (eolpos != labelpos && eolpos[-1] == '\r')
                eolpos[-1] = '\0';
            if (strcasecmp(labelpos, label))
                continue;
            inf_recording_pos = ftello(inf);
            return true;
        }
    }
    fclose(inf);
    inf = NULL;
    return false;
}

void *SensorStateThread::run()
{
    while(!stopping)
    {
        switch(state)
        {
            case SST_INITIALISING:
                do_initialising();
                break;
            case SST_CONFIGURING:
                do_configuring();
                break;
            case SST_WAITING_FOR_DATA:
                do_waitingfordata();
                break;
            case SST_WAITING_FOR_SENSOR:
                do_waitingforsensor();
                break;
            case SST_RECEIVING:
                do_receiving();
                break;
            case SST_REPLAYING:
                do_replaying();
                break;
            case SST_SENSOR_NOT_FOUND:
                do_notfound();
                break;
        }
    }
    return NULL;
}

void SensorStateThread::do_initialising()
{
    if (inf)
    {
        state = SST_REPLAYING;
        return;
    }
    last_error = init_openucd_and_module(handle);
    if (last_error)
        state = SST_SENSOR_NOT_FOUND;
    else
        state = SST_CONFIGURING;
}

void SensorStateThread::do_configuring()
{
    last_error = configure_sensor(handle);
    if (last_error == ERR_NOSENSOR)
        state = SST_WAITING_FOR_SENSOR;
    else if (last_error && last_error != ERR_NOSENSOR)
        state = SST_SENSOR_NOT_FOUND;
    else
        state = SST_WAITING_FOR_DATA;
}

void SensorStateThread::do_waitingfordata()
{
    OPIPKT_t pkt;
    int rc = opiucd_getwltsdata(&handle, &pkt);
    if (rc == 1)
    {
        state = SST_RECEIVING;
        last_signal = time(NULL);
        return;
    }
    // Check if controller is still connected
    rc = opiucd_status(&handle, &pkt);
    if (rc < 0)
    {
        state = SST_INITIALISING;
        return;
    }
    usleep(10000);
}

void SensorStateThread::do_waitingforsensor()
{
    OPIPKT_t pkt;
    int rc = opiucd_getwltsdata(&handle, &pkt);
    if (rc == 1)
    {
        state = SST_RECEIVING;
        last_signal = time(NULL);
        return;
    }
    rc = opiucd_tsstatus(&handle, &pkt);
    if (rc == 0)
    {
        state = SST_CONFIGURING;
        return;
    }
    rc = opiucd_status(&handle, &pkt);
    if (rc < 0)
    {
        state = SST_INITIALISING;
        return;
    }
    usleep(10000);
}

void SensorStateThread::do_receiving()
{
    OPIPKT_t pkt;
    int rc = opiucd_getwltsdata(&handle, &pkt);
    if (rc == 1)
    {
        last_signal = time(NULL);
        SensorDataPacket sdp;
        if (data_packet_to_sdp(pkt, sdp))
            process_data(pkt, sdp);
        // interpret_data_packet(pkt);
    }
    else
    {
        usleep(1000);
        if (time(NULL) - last_signal > 3)
        {
            state = SST_WAITING_FOR_DATA;
            return;
        }
    }
}

void SensorStateThread::do_replaying()
{
    while(true)
    {
        char buf[1024];
        if (!fgets(buf, 1024, inf))
            break;
        if (buf[0] == '+')
        {
            OPIPKT_t pkt;
            pkt.length = 0;
            int pos = 1;
            while(isxdigit(buf[pos]) && isxdigit(buf[pos + 1]))
            {
                unsigned byteval;
                sscanf(&buf[pos], "%2x", &byteval);
                pkt.payload[pkt.length++] = byteval;
                pos += 2;
            }
            if (pos)
            {
                pkt.dataCode = pkt.payload[0];
                interpret_data_packet(pkt);
                SensorDataPacket sdp;
                if (data_packet_to_sdp(pkt, sdp))
                    process_data(pkt, sdp);
            }
            playback_frame++;
            usleep(1000000 / 8);
            return;
        }
        if (buf[0] == '-')
            break;
    }
    playback_frame = 0;
    fseeko(inf, inf_recording_pos, SEEK_SET);
}

void SensorStateThread::do_notfound()
{
    // Wait 2s before trying again
    for (int i = 0; i < 20 && !stopping; ++i)
        usleep(100000);
    state = SST_INITIALISING;
}

string SensorStateThread::get_status_text() const
{
    switch(state)
    {
        case SST_INITIALISING:
            return "Initialising.";
        case SST_CONFIGURING:
            return "Configuring the sensor.";
        case SST_WAITING_FOR_SENSOR:
            return "Waiting for the sensor to be plugged in or to send data.";
        case SST_WAITING_FOR_DATA:
            return "Waiting for the sensor to send data.";
        case SST_RECEIVING:
            return "Receiving data.";
        case SST_REPLAYING:
            return "Replaying recorded data.";
        case SST_SENSOR_NOT_FOUND:
            return "Error: " + string(get_sensor_error_string(last_error));
    }
    return "Unknown state.";
}

void SensorStateThread::start()
{
    pthread_create(&thread, NULL, run_func, this);
}

void SensorStateThread::stop()
{
    if (handle)
    {
        opiucd_turnmodoff(&handle);
        opiucd_offmode(&handle);
    }
    stopping = true;
    pthread_join(thread, NULL);
}

