#include "sensor_thread.h"
#include <sstream>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

using namespace std;

SensorStateProcessor::SensorStateProcessor()
{
    aout = NULL;
    sock = -1;
    data_ptr = 0;

    send_addr.sin_family = AF_INET;
    send_addr.sin_port = ntohs(9999);
    send_addr.sin_addr.s_addr = 0;
    is_recording = false;
    recording_fd = -1;
    rec_frames = 0;

    pthread_mutex_init(&sensor_mutex, NULL);
    
    open_socket();
}

void SensorStateProcessor::set_audio_output(AudioOutput *_aout)
{
    pthread_mutex_lock(&sensor_mutex);
    aout = _aout;
    pthread_mutex_unlock(&sensor_mutex);
}

void SensorStateProcessor::sendblob(const void *blob, uint16_t len)
{
    if (sendto(sock, blob, len, 0, (sockaddr *)&send_addr, sizeof(send_addr)) != len)
        perror("sendto");
}

void SensorStateProcessor::write_string_to_recording(const string &s)
{
    int ofs = 0;
    while(ofs < s.length())
    {
        int res = write(recording_fd, s.c_str() + ofs, s.length() - ofs);
        if (res < 0)
        {
            if (res == EINTR) // interrupted system call
                continue;
            fprintf(stderr, "Problem while recording to %d: %s\n", recording_fd, strerror(errno));
            return;
        }
        else
        {
            ofs += res;
        }
    }
    fsync(recording_fd);
}

#define debug_printf(...)

void SensorStateProcessor::process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp)
{
    pthread_mutex_lock(&sensor_mutex);
    if (is_recording)
    {
        stringstream line;
        line << "+";
        for (int i = 0; i < pkt.length; ++i)
        {
            line.fill('0');
            line.width(2);
            line << hex << right << (unsigned)pkt.payload[i];
        }
        line << endl;
        write_string_to_recording(line.str());
        rec_frames++;
    }
    pthread_mutex_unlock(&sensor_mutex);
    // Sometimes packets arrive with all zeros in the ADC fields, discard those 
    bool has_data = false;
    // 
    int first_valid_sample = 0;
    debug_printf("PDN: %d\n", (int)sdp.frame_pdn);
    debug_printf("Timestamp: %d\n", (int)sdp.timestamp_bytes[4]);
    debug_printf("Data count %d: ", (int)sdp.data_count);
    for (int i = first_valid_sample; i < sdp.data_count; i++)
    {
        if (sdp.data[i])
        {
            has_data = true;
            break;
        }
    }
    debug_printf("\n");
    for (int i = first_valid_sample; i < sdp.data_count; i++)
    {
        debug_printf("%d, ", sdp.data[i]);
        data[data_ptr] = sdp.data[i] / 16383.0;
        data_ptr = (data_ptr + 1) & 511;
    }
    debug_printf("\n");
    if (!has_data)
        return;
    
    dataqueue += string((const char *)sdp.data, sdp.data_count * sizeof(sdp.data[0]));
    if (dataqueue.length() > 256)
        dataqueue.erase(0, dataqueue.length() - 256);
    sendblob(dataqueue.c_str(), dataqueue.length());
    
    pthread_mutex_lock(&sensor_mutex);
    if (aout)
    {
        int frames = (sdp.data_count - first_valid_sample);
        int delay = 0;
        for (int ng = 0; ng < 10; ng++)
        {
            Grain *g = aout->get_grain_to_fill(frames / 2, delay);
            if (g)
            {
                g->data.clear();
                int repeat = 1;
                int stretch = (44100 / 512) / 4;
                int grainlen = frames * stretch * repeat;
                g->data.resize(grainlen);
                float envlen = 200.0;
                for (int i = 0; i < grainlen; ++i)
                {
                    float gain = 1.0;
                    if (i < envlen && i < grainlen / 2)
                        gain = i / envlen;
                    else if (i >= grainlen - envlen)
                        gain = (grainlen - i) / envlen;
                    int s1 = sdp.data[(i / stretch) % frames];
                    int s2 = sdp.data[(i / stretch + 1) % frames];
                    g->data[i] = (s1 + float(s2 - s1) * (i % stretch) / stretch) * gain * gain / 16383.0;
                }
                g->delay = delay;
                g->readpos = 0;
                g->play = true;
                if (delay > 44100 / 4)
                    break;
            }
            else
                break;
        }
    }
    pthread_mutex_unlock(&sensor_mutex);
}

void SensorStateProcessor::open_socket()
{
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        perror("socket");

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = 0;
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(sock, (sockaddr *)&addr, sizeof(addr)))
        perror("bind");
}

void SensorStateProcessor::start_recording(const char *label)
{
    assert(!is_recording);
    assert(label);
    pthread_mutex_lock(&sensor_mutex);
    recording_fd = open("brainwave-recordings", O_APPEND | O_CREAT | O_WRONLY, 0660);
    if (recording_fd < 0)
    {
        perror("open");
    }
    else
    {
        stringstream ss;
        ss << "#" << time(NULL) << " " << label << endl;
        write_string_to_recording(ss.str());
        rec_frames = 0;
        rec_label = label;
        is_recording = true;
    }
    pthread_mutex_unlock(&sensor_mutex);
}

void SensorStateProcessor::stop_recording()
{
    if (!is_recording)
        return;
    pthread_mutex_lock(&sensor_mutex);
    is_recording = false;
    stringstream ss;
    ss << "-" << time(NULL) << " stop" << endl;
    write_string_to_recording(ss.str());
    close(recording_fd);
    pthread_mutex_unlock(&sensor_mutex);
}

void SensorStateProcessor::stop()
{
    // Stop recording as early as possible in order to have a sane end timestamp
    if (is_recording)
        stop_recording();
    SensorStateThread::stop();
}

SensorStateProcessor::~SensorStateProcessor()
{
    if (is_recording)
        stop_recording();
    if (sock >= 0)
        close(sock);
}
