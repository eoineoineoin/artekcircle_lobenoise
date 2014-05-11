#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/types.h>
#include <sndfile.h>
#include <opi_linux.h>

// RBJ bandpass code shamelessly taken from Calf
// with crappy envelope follower slapped on it
struct bandpass
{
    double a0, a1, a2, b1, b2;
    double x1, x2, y1, y2;
    float levelTracker;
    bandpass(double fc, double bw,double esr = 44100.0, double gain=1.0)
    {
        double q = fc / bw;
        double omega=(double)(2*M_PI*fc/esr);
        double sn=sin(omega);
        double cs=cos(omega);
        double alpha=(double)(sn/(2*q));

        double inv=(double)(1.0/(1.0+alpha));

        a0 =  (double)(gain*inv*alpha);
        a1 =  0.f;
        a2 =  (double)(-gain*inv*alpha);
        b1 =  (double)(-2*cs*inv);
        b2 =  (double)((1 - alpha)*inv);
        levelTracker = 0.f;
    }
    double processFilter(double in)
    {
        double out = in * a0 + x1 * a1 + x2 * a2 - y1 * b1 - y2 * b2;
        x2 = x1;
        y2 = y1;
        x1 = in;
        y1 = out;
        return out;
    }
    double process(double in)
    {
        in = processFilter(in);
        in = fabs(in);
        // this value (roughly) corresponds to the cutoff frequency of the envelope follower
        float avg = (in > levelTracker) ? 0.05 : 0.01;
        levelTracker += (in - levelTracker) * avg;
        return levelTracker;
    }
};

struct sinewave
{
    double phase;
    sinewave()
    : phase(0.0)
    {
    }
    float generate(float freq)
    {
        //float out = sin(phase) + sin(2 * phase) / 2 + sin(3 * phase) /3 +sin(4 * phase) /4 ;
        float out = phase / (2 * M_PI);
        phase += freq* 2 * M_PI / 44100;
        if (phase >= 2 * M_PI)
            phase = fmod(phase, 2 * M_PI);
        return out;
    }
};

float tune(float val)
{
    val = trunc(16 * val);
    return pow(2.0, val / 12.0);
}

struct tracker
{
    bandpass alpha, beta, theta, delta;
    bandpass alpha2, beta2, theta2, delta2;
    sinewave alphaGen, betaGen, thetaGen, deltaGen;
    tracker()
    : alpha(11, 3.0)
    , beta(24, 8.0)
    , theta(5.5, 1.5)
    , delta(2.0, 2.0)
    , alpha2(11, 3.0)
    , beta2(24, 8.0)
    , theta2(5.5, 1.5)
    , delta2(2.0, 2.0)
    {
    }
    float process(float val)
    {
        // get levels of subbands
        float aval = alpha2.process(alpha.process(val));
        float bval = beta2.process(beta.process(val));
        float tval = theta2.process(theta.process(val)) ;
        float dval = delta2.process(delta.process(val));
        
        float out = 0.0;
        // whether or not to do the frequency modulation as well as well as amplitude modulation
        bool do_fm = false;
        // static float freq1 = 440.0 * pow(2.0, 15.0 / 12.0);
        // static float freq2 = 440.0 * pow(2.0, 26.0 / 12.0);
        // static float freq3 = 440.0 * pow(2.0, 0.0 / 12.0);
        // static float freq4 = 440.0 * pow(2.0, -24.0 / 12.0);
        static float freq1 = 440.0 * pow(2.0, 3.0 / 12.0);
        static float freq2 = 440.0 * pow(2.0, 7.0 / 12.0);
        static float freq3 = 440.0 * pow(2.0, 0.0 / 12.0);
        static float freq4 = 440.0 * pow(2.0, 14.0 / 12.0);
        out += aval * alphaGen.generate(freq1 * pow(2.0, tune(aval)));
        out += bval * betaGen.generate(freq2  * pow(2.0, tune(bval)));
        out += tval * thetaGen.generate(freq3  * pow(2.0, tune(tval)));
        out += dval * deltaGen.generate(freq4 * pow(2.0, tune(dval)));
        
        out *= 3;
        if (out < -1.0)
            out = -1.0;
        if (out > 1.0)
            out = 1.0;
        return out;
    }
};

struct agc
{
    double gain;
    agc()
    {
        gain = 1.0;
    }
    float process(float val)
    {
        val *= gain;
        if (val > 1.0)
            gain *= 0.98;
        else if (gain < 1)
            gain *= 1.001;
        return val;
    }
};
    
jack_port_t *port;
SNDFILE *sndf;
SF_INFO info;
float *data;
uint32_t nframes;
uint32_t playpos = 0;
uint32_t slowdown = 88;
tracker trk;
agc gainer;

int callback(unsigned size, void *arg)
{
    float *outdata = (float *)jack_port_get_buffer(port, size);
    printf("%f%% %0.6f %0.6f %0.6f %0.6f\n", playpos * 100.0 / (nframes * slowdown), trk.alpha.levelTracker, trk.beta.levelTracker, trk.theta.levelTracker, trk.delta.levelTracker );
    for (int i = 0; i < size; i++)
    {
        int smppos = playpos / slowdown;
        float val = data[smppos] + (data[smppos + 1] - data[smppos]) * (playpos - smppos * slowdown) / slowdown;
        val = gainer.process(8 * val);
        val = trk.process(val);
        outdata[i] = val;
        playpos++;
        if (playpos >= slowdown * nframes - 1)
            playpos = 0;
    }
    return 0;
}

int comport = -1;

int initeeg()
{
    if (opi_openucd_com(&comport) < 0)
    {
        fprintf(stderr, "Cannot initialise the EEG\n");
        return 0;
    }
    if (opiucd_onmode(&comport)< 0)
    {
        fprintf(stderr, "Cannot initialise the controller\n");
        return 0;
    }
    if (opiucd_turnmodon(&comport)< 0)
    {
        fprintf(stderr, "Cannot turn on the module\n");
        return 0;
    }
    return 1;
}


int main(int argc, char *argv[])
{
    if (argc == 2)
    {
        sndf = sf_open(argv[1], SFM_READ, &info);
        nframes = info.frames;
        data = (float *)malloc(sizeof(float) * nframes);
        printf("Reading data from file, ptr=%p len=%d\n", sndf, nframes);
        sf_readf_float(sndf, data, nframes);
    }
    else
    {
        if (!initeeg())
        {
            fprintf(stderr, "Cannot switch EEG on\n");
            return 1;
        }
        // do EEGey stuff here
        while(1)
        {
            OPIPKT_t pkt;
            opiucd_getwltsdata(&comport, &pkt);
            for (int i = 0; i < pkt.length; i++)
                printf("[%d] %d\n", i, (unsigned)pkt.payload[i]);
            printf("----\n");
        }
    }
    jack_status_t status;
    jack_client_t *client = jack_client_open("brainwaves", (jack_options_t)0, &status);
    assert(client);
    port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    assert(port);
    jack_set_process_callback(client, callback, NULL);
    jack_activate(client);
    jack_connect(client, "brainwaves:output", "system:playback_1");
    jack_connect(client, "brainwaves:output", "system:playback_2");
    while(1)
        sleep(1);
    jack_port_unregister(client, port);
    jack_client_close(client);
    
    return 0;
}
