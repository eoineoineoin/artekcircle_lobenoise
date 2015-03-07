#include "audio_output.h"
#include "fsm.h"
#include <netinet/ip.h>

class SensorStateProcessor: public SensorStateThread
{
public:
    AudioOutput *aout;
    pthread_mutex_t sensor_mutex; 
    int sock;
    bool is_recording;
    int recording_fd;
    std::string dataqueue;
    std::string rec_label;
    float data[512];
    int data_ptr;
    int rec_frames;

public:
    SensorStateProcessor();
    void start_recording(const char *label);
    void stop_recording();
    bool get_is_recording() const { return is_recording; }
    std::string get_rec_label() const { return is_recording ? rec_label : std::string(); }
    void set_audio_output(AudioOutput *_aout);
    void write_string_to_recording(const std::string &s);

    virtual void process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp);
    virtual void stop();

    ~SensorStateProcessor();

private:
    sockaddr_in send_addr;

private:
    void open_socket();
    void sendblob(const void *blob, uint16_t len);
};

