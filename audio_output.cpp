#include "audio_output.h"

Grain::Grain()
{
    play = false;
    readpos = 0;
    delay = 0;
}

AudioOutput::AudioOutput()
{
    client = NULL;
    gain = 0.f; // will be set by the client code
    
    grains.resize(40);
    
    jack_status_t status;
    jack_client_t *tmp_client = jack_client_open("brainwaves", (jack_options_t)0, &status);
    if (tmp_client)
    {
        port = jack_port_register(tmp_client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (port)
        {
            jack_set_process_callback(tmp_client, AudioOutput::process, this);
            jack_activate(tmp_client);
            jack_connect(tmp_client, "brainwaves:output", "system:playback_1");
            jack_connect(tmp_client, "brainwaves:output", "system:playback_2");
            client = tmp_client;
        }
        else
            jack_client_close(tmp_client);
    }
}

Grain *AudioOutput::get_grain_to_fill(int overlap, int &maxdelay)
{
    maxdelay = 0;
    for(size_t i = 0; i < grains.size(); ++i)
    {
        int end = grains[i].delay + grains[i].data.size() - overlap;
        if (end > maxdelay)
            maxdelay = end;
    }
    for(size_t i = 0; i < grains.size(); ++i)
    {
        if (!grains[i].play)
            return &grains[i];
    }
    return NULL;
}

void AudioOutput::mix(float *buffer, unsigned nsamples)
{
    for (unsigned g = 0; g < grains.size(); ++g)
    {
        Grain &grain = grains[g];
        if (!grain.play)
            continue;
        if (grain.delay >= nsamples)
        {
            grain.delay -= nsamples;
            continue;
        }
        for (unsigned i = grain.delay; i < nsamples && grain.readpos < grain.data.size(); ++i)
        {
            buffer[i] += gain * grain.data[grain.readpos++];
        }
        grain.delay = 0;
        if (grain.readpos >= grain.data.size())
            grain.play = false;
    }
}

int AudioOutput::process(unsigned size, void *arg)
{
    AudioOutput *self = (AudioOutput *)arg;
    float *outdata = (float *)jack_port_get_buffer(self->port, size);
    for (unsigned i = 0; i < size; ++i)
    {
        outdata[i] = 0.f;
    }
    self->mix(outdata, size);
    return 0;
}

AudioOutput::~AudioOutput()
{
    if (client)
        jack_client_close(client);
}

