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
#include "fft.h"
#include "fsm.h"

using namespace std;

float data[512];
int data_ptr = 0;

class SensorStateProcessor: public SensorStateThread
{
public:
    virtual void process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp);
};

SensorStateProcessor sensor_thread;

void SensorStateProcessor::process_data(const OPIPKT_t &pkt, const SensorDataPacket &sdp)
{
    for (int i = 0; i < sdp.data_count; i++)
    {
        data[data_ptr] = sdp.data[i] / 16383.0;
        data_ptr = (data_ptr + 1) & 511;
    }
}

void draw(GtkWidget *dra, cairo_t *cr, gpointer user_data)
{
    if (sensor_thread.get_state() != SST_RECEIVING)
        return;
    guint width, height;
    GdkRGBA color;
    typedef complex<float> fcomplex;
    dsp::fft<float, 9> fourier;
    
    fcomplex input[512], output[512];
    float avg = 0;
    for (int i = 0; i < 512; ++i)
    {
        float val = data[i];
        input[i] = val;
        avg += val;
    }
    avg /= 512;
    fourier.calculate(input, output, false);
    
    width = gtk_widget_get_allocated_width (dra);
    height = gtk_widget_get_allocated_height (dra);

    for (int i = 0; i < width; i++)
    {
        int pt = i * 100 / (width - 1);
        float ptv = log10(abs(output[pt]) + 0.00000000001) / 2;
        float pty = height * (1 - ptv);
        if (i == 0)
            cairo_move_to(cr, i, pty);
        else
            cairo_line_to(cr, i, pty);
    }
    cairo_line_to(cr, width, height);
    cairo_line_to(cr, 0, height);

    color.red = 0.0;
    color.green = 255.0;
    color.blue = 127.0;
    color.alpha = 1.0;
    gdk_cairo_set_source_rgba (cr, &color);

    cairo_fill (cr);    

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
    color.red = 0.0;
    color.green = 1.0;
    color.blue = 0.0;
    color.alpha = 1.0;
    gdk_cairo_set_source_rgba (cr, &color);

    cairo_stroke (cr);    
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
    sensor_thread.start();
    gtk_init(&argc, &argv);
    GtkWidget *win = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *dra = gtk_drawing_area_new();
    status_widget = gtk_label_new("Status");
    gtk_widget_set_size_request(dra, 1024, 512);
    gtk_box_pack_start(GTK_BOX(vbox), status_widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), dra, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    GdkColor black = {0, 0, 0, 0};
    gtk_widget_modify_bg(dra, GTK_STATE_NORMAL, &black);
    gtk_widget_show(dra);
    gtk_widget_show(status_widget);
    gtk_widget_show(vbox);
    gtk_widget_show(win);
    g_signal_connect(G_OBJECT(win), "delete-event", G_CALLBACK(gtk_false), NULL);
    g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(dra), "draw", G_CALLBACK(draw), NULL);
    
    g_idle_add(my_idle_func, dra);
    gtk_main();
    sensor_thread.stop();
    
    return 0;
}
