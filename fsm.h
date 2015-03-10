#ifndef SENSOR_FSM_H
#define SENSOR_FSM_H

#include <pthread.h>
#include <string>
#include "sensor.h"

enum SensorState
{
    SST_INITIALISING,
    SST_CONFIGURING,
    SST_WAITING_FOR_SENSOR,
    SST_WAITING_FOR_DATA,
    SST_RECEIVING,
    SST_REPLAYING,
    SST_SENSOR_NOT_FOUND, 
};

class SensorStateThread
{
protected:
    pthread_t thread;
    SensorState state;
    volatile bool stopping;
    HANDLE handle;
    SensorError last_error;
    time_t last_signal;
    FILE *inf;
    uint64_t inf_recording_pos;
    int playback_frame;
    void *run();
    static void *run_func(void *pThread) { return ((SensorStateThread *)pThread)->run(); }

    void do_initialising();
    void do_configuring();
    void do_receiving();
    void do_replaying();
    void do_waitingforsensor();
    void do_waitingfordata();
    void do_notfound();
public:
    SensorStateThread();
    bool set_file_input(const char *label);
    void start();
    virtual void stop();
    SensorState get_state() const { return state; }
    int get_last_error() const { return last_error; }
    std::string get_status_text() const;
    std::string get_recording_path() const;
    virtual void process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp) {}
    virtual ~SensorStateThread() {}
};

#endif
