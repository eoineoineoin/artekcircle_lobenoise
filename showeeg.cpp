#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/types.h>
#include <sndfile.h>
#include <opi_linux.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <netinet/ip.h>
#include "fft.h"
#include "fsm.h"

class AudioOutput
{
protected:
    jack_client_t *client;
    jack_port_t *port;
public:
    AudioOutput();
    bool is_ok() const { return client != NULL; }
    static int process(unsigned size, void *arg);
    ~AudioOutput();
};

int AudioOutput::process(unsigned size, void *arg)
{
    AudioOutput *self = (AudioOutput *)arg;
    float *outdata = (float *)jack_port_get_buffer(self->port, size);
    for (unsigned i = 0; i < size; ++i)
    {
        outdata[i] = 0.f;
    }
    return 0;
}

AudioOutput::AudioOutput()
{
    client = NULL;
    
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

AudioOutput::~AudioOutput()
{
    if (client)
        jack_client_close(client);
}

using namespace std;

float data[512];
int data_ptr = 0;
GtkAdjustment *gain_adjustment;
int sock;
sockaddr_in send_addr;

class SensorStateProcessor: public SensorStateThread
{
public:
    virtual void process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp);
};

SensorStateProcessor sensor_thread;

void sendblob(const void *blob, uint16_t len)
{
    if (sendto(sock, blob, len, 0, (sockaddr *)&send_addr, sizeof(send_addr)) != len)
        perror("sendto");
}

void SensorStateProcessor::process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp)
{
    // Sometimes packets arrive with all zeros in the ADC fields, discard those 
    bool has_data = false;
    // 
    int first_valid_sample = 3;
    for (int i = first_valid_sample; i < sdp.data_count; i++)
    {
        if (sdp.data[i])
        {
            has_data = true;
            break;
        }
    }
    printf("\n");
    if (!has_data)
        return;
    for (int i = first_valid_sample; i < sdp.data_count; i++)
    {
        printf("%d, ", sdp.data[i]);
        data[data_ptr] = sdp.data[i] / 16383.0;
        data_ptr = (data_ptr + 1) & 511;
    }
    sendblob(sdp.data, sdp.data_count * sizeof(sdp.data[0]));
}

void draw(GtkWidget *dra, cairo_t *cr, gpointer user_data)
{
    SensorState ss = sensor_thread.get_state();
    if (ss != SST_RECEIVING && ss != SST_WAITING_FOR_DATA)
        return;
    guint width, height;
    GdkRGBA color;
    typedef complex<float> fcomplex;
    dsp::fft<float, 9> fourier;
    
    fcomplex input[512], output[512];
    float avg = 0;
    float gain = pow(2.0, gtk_adjustment_get_value(gain_adjustment) / 6.0);
    for (int i = 0; i < 512; ++i)
    {
        float val = gain * data[i];
        input[i] = val;
        avg += val;
    }
    avg /= 512;
    fourier.calculate(input, output, false);

    // Determine the relative signal level in 'good' and 'bad' bands, scale
    // them to size of those bands
    float good = 0, bad = 0;
    int threshold = 30;
    for (int i = 1; i < 100; ++i)
    {
        if (i < threshold)
            good += abs(output[i]) / (threshold - 1);
        else
            bad += abs(output[i]) / (99 - threshold);
    }
    
    width = gtk_widget_get_allocated_width (dra);
    height = gtk_widget_get_allocated_height (dra);


    for (int i = 0; i < width; i++)
    {
        int pt = i * 100 / (width - 1);
        const char *colorspec = "rgba(0,63,127,0.5)";

        // theta
        if (pt >= 1 && pt <= 3)
            colorspec = "grey";
        // theta
        if (pt >= 4 && pt <= 7)
            colorspec = "blue";
        // alpha
        if (pt >= 8 && pt <= 15)
            colorspec = "green";
        // beta
        if (pt >= 16 && pt <= 31)
            colorspec = "orange";
        // 50 Hz or 60 Hz ground hum
        if ((pt >= 49 && pt <= 51) || (pt >= 59 && pt <= 61))
            colorspec = "red";
        gdk_rgba_parse(&color, colorspec);
        gdk_cairo_set_source_rgba (cr, &color);

        float ptv = log10(abs(output[pt]) + 0.00000000001) / 2;
        float pty = height * (1 - ptv);
        cairo_move_to(cr, i, height);
        cairo_line_to(cr, i, pty);
        cairo_stroke(cr);
    }

    for (int i = 0; i < 512; i++)
    {
        float ptv = (input[i].real() - avg);
        float pty = height * (1 - ptv) / 2;
        int x = i * (width - 1) / 511;
        if (i == 0)
            cairo_move_to(cr, x, pty);
        else
            cairo_line_to(cr, x, pty);
    }

    //gtk_style_context_get_color (gtk_widget_get_style_context (dra),
    //                           (GtkStateFlags)0,
    //                           &color);
    bool is_good = good > 4 * bad && good >= gain / 4 && bad < 2 * gain;
    color.red = is_good ? 0.0 : 1.0;
    color.green = is_good ? 1.0 : 0.0;
    color.blue = 0.0;
    color.alpha = 1.0;
    gdk_cairo_set_source_rgba (cr, &color);

    cairo_stroke (cr);    

    color.red = 1.0;
    color.green = 1.0;
    color.blue = 1.0;

    for (int i = 0; i < 100; i += 5)
    {
        int x = i * (width - 1) / 100;
        if (i % 10 == 5)
            color.alpha = 0.25;
        else
            color.alpha = 0.5;
        gdk_cairo_set_source_rgba (cr, &color);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);
    }
}

GtkWidget *status_widget;

gboolean my_idle_func(gpointer user_data)
{
    GtkWidget *w = (GtkWidget *)user_data;
    gtk_widget_queue_draw(w);
    gtk_label_set_text(GTK_LABEL(status_widget), sensor_thread.get_status_text().c_str());
    return TRUE;
}


int main(int argc, char *argv[])
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

    send_addr.sin_family = AF_INET;
    send_addr.sin_port = ntohs(9999);
    send_addr.sin_addr.s_addr = 0;

    sensor_thread.start();
    gtk_init(&argc, &argv);
    GtkWidget *win = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *dra = gtk_drawing_area_new();
    gain_adjustment = gtk_adjustment_new(0, -36, 36, 1, 6, 6);
    GtkWidget *slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, gain_adjustment);
    status_widget = gtk_label_new("Status");
    gtk_widget_set_size_request(dra, 1024, 512);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Gain [dB]"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), slider, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), status_widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), dra, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    GdkRGBA black = {0, 0, 0, 1};
    gtk_widget_override_background_color(dra, GTK_STATE_FLAG_NORMAL, &black);
    gtk_widget_show_all(win);
    g_signal_connect(G_OBJECT(win), "delete-event", G_CALLBACK(gtk_false), NULL);
    g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(dra), "draw", G_CALLBACK(draw), NULL);
    
    g_idle_add(my_idle_func, dra);
    gtk_main();
    sensor_thread.stop();
    
    return 0;
}
