#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sndfile.h>
#include <opi_linux.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <vector>
#include "fft.h"
#include "fsm.h"
#include "sensor_thread.h"

using namespace std;

SensorStateProcessor sensor_thread;
GtkWidget *main_window;
GtkWidget *status_widget;
GtkWidget *drawing_area;
GtkWidget *record_button;
GtkWidget *stop_button;

using namespace std;

GtkAdjustment *gain_adjustment;

void draw(GtkWidget *dra, cairo_t *cr, gpointer user_data)
{
    SensorStateProcessor *ssp = (SensorStateProcessor *)user_data;
    SensorState ss = ssp->get_state();
    if (ss != SST_RECEIVING && ss != SST_WAITING_FOR_DATA && ss != SST_REPLAYING)
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
        float val = gain * sensor_thread.data[i];
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

gboolean my_idle_func(gpointer user_data)
{
    SensorStateProcessor *ssp = (SensorStateProcessor *)user_data;
    gtk_widget_queue_draw(drawing_area);
    stringstream stext;
    stext << ssp->get_status_text();
    if (sensor_thread.get_is_recording())
    {
        stext << " Recording as '" << sensor_thread.get_rec_label() << "' (" << sensor_thread.rec_frames << " frames written so far).";
    }
    if (sensor_thread.get_is_playing_back())
    {
        stext << " Current frame: " << sensor_thread.get_playback_frame();
    }
    gtk_label_set_text(GTK_LABEL(status_widget), stext.str().c_str());
    return TRUE;
}

void update_record_controls()
{
    bool is_recording = sensor_thread.get_is_recording();
    gtk_widget_set_sensitive(record_button, sensor_thread.can_record() && !is_recording);
    gtk_widget_set_sensitive(stop_button, is_recording);
}

void record_button_clicked(GtkWidget *widget, gpointer ptr)
{
    if (sensor_thread.get_is_recording())
        return;
    GtkWidget *dialog;
    GtkDialogFlags flags = (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT);
    dialog = gtk_dialog_new_with_buttons ("Record biosensor data",
                                          GTK_WINDOW(main_window),
                                          flags,
                                          "_Cancel",
                                          GTK_RESPONSE_CANCEL,
                                          "_OK",
                                          GTK_RESPONSE_OK,
                                          NULL);
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Identifier:"), FALSE, FALSE, 5);
    GtkWidget *entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    GtkBox *content = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    gtk_box_pack_start(content, hbox, FALSE, FALSE, 5);
    gtk_widget_show_all(GTK_WIDGET(content));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
    {
        sensor_thread.start_recording(gtk_entry_get_text(GTK_ENTRY(entry)));
        update_record_controls();
    }
    gtk_widget_destroy(dialog);
}

void stop_button_clicked(GtkWidget *widget, gpointer ptr)
{
    sensor_thread.stop_recording();
    update_record_controls();
}

#if GTK_CHECK_VERSION(3,10,0)
#define gtk_button_new_compatible(stock_name) gtk_button_new_from_icon_name(stock_name, GTK_ICON_SIZE_BUTTON)
#else
#define gtk_button_new_compatible(stock_name) gtk_button_new_from_stock(stock_name)
#endif

void create_ui()
{
    main_window = GTK_WIDGET(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 1024, 512);

    gain_adjustment = gtk_adjustment_new(0, -36, 36, 1, 6, 6);
    GtkWidget *slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, gain_adjustment);
    record_button = gtk_button_new_compatible("media-record");
    stop_button = gtk_button_new_compatible("media-playback-stop");
    
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Gain [dB]"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), slider, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), record_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), stop_button, FALSE, FALSE, 0);
        
    status_widget = gtk_label_new("Status");
    gtk_box_pack_start(GTK_BOX(vbox), status_widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    
    gtk_container_add(GTK_CONTAINER(main_window), vbox);
    GdkRGBA black = {0, 0, 0, 1};
    gtk_widget_override_background_color(drawing_area, GTK_STATE_FLAG_NORMAL, &black);
    gtk_widget_show_all(main_window);
    g_signal_connect(G_OBJECT(main_window), "delete-event", G_CALLBACK(gtk_false), NULL);
    g_signal_connect(G_OBJECT(main_window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(record_button), "clicked", G_CALLBACK(record_button_clicked), NULL);
    g_signal_connect(G_OBJECT(stop_button), "clicked", G_CALLBACK(stop_button_clicked), NULL);
    update_record_controls();
}

void run_main_loop()
{
    AudioOutput ao;
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw), &sensor_thread);
    g_idle_add(my_idle_func, &sensor_thread);

    sensor_thread.start();
    sensor_thread.set_audio_output(&ao);
    gtk_main();
    sensor_thread.stop();
}

int main(int argc, char *argv[])
{
    load_sensor_config();

    gtk_init(&argc, &argv);
    
    if (argc > 1)
    {
        if (!sensor_thread.set_file_input(argv[1]))
        {
            fprintf(stderr, "Recording '%s' has not been found.", argv[1]);
            return 1;
        }
    }
    
    create_ui();
    run_main_loop();

    return 0;
}
