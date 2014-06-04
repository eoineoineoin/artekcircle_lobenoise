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
    void *run();
    static void *run_func(void *pThread) { return ((SensorStateThread *)pThread)->run(); }

    void do_initialising();
    void do_configuring();
    void do_receiving();
    void do_waitingforsensor();
    void do_waitingfordata();
    void do_notfound();
public:
    SensorStateThread();
    void start();
    void stop();
    SensorState get_state() const { return state; }
    int get_last_error() const { return last_error; }
    std::string get_status_text() const;
};

#endif
