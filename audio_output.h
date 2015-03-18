#include <jack/jack.h>
#include <jack/types.h>
#include <vector>

struct Grain
{
    std::vector<float> data;
    bool play;
    int readpos;
    int delay;
    
    Grain();
};

class AudioOutput
{
protected:
    jack_client_t *client;
    jack_port_t *port;
    std::vector<Grain> grains;
    float gain;
public:
    AudioOutput();
    bool is_ok() const { return client != NULL; }
    void set_gain(float gain) { this->gain = gain; }
    void mix(float *buffer, unsigned nsamples);
    static int process(unsigned size, void *arg);
    Grain *get_grain_to_fill(int overlap, int &maxdelay);
    ~AudioOutput();
};

