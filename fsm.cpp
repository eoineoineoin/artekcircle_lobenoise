#include "fsm.h"
#include <unistd.h>

using namespace std;

SensorStateThread::SensorStateThread()
{
    last_error = ERR_OK;
    state = SST_INITIALISING;
    stopping = false;
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
            case SST_SENSOR_NOT_FOUND:
                do_notfound();
                break;
        }
    }
    return NULL;
}

void SensorStateThread::do_initialising()
{
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
        return;
    }
    rc = opiucd_tsstatus(&handle, &pkt);
    if (rc == 0)
    {
        state = SST_CONFIGURING;
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
        SensorDataPacket sdp;
        if (data_packet_to_sdp(pkt, sdp))
            process_data(pkt, sdp);
        // interpret_data_packet(pkt);
    }
    else
        usleep(1000);
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
    stopping = true;
    pthread_join(thread, NULL);
}

