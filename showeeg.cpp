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
#include <map>
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

typedef complex<float> fcomplex;
string last_msg;
float last_msg_age;

string virtual_chris(fcomplex waveform[512], fcomplex spectrum[512], float gain)
{
    string verdict;
    int spikes = 0;
    for (int i = 0; i < 512; i += 64)
    {
        bool empty = true;
        float maxdelta = 0;
        for (int j = 0; j < 64; ++j)
        {
            if (j > 5)
                maxdelta = max(maxdelta, abs(waveform[i + j + 1] - waveform[i + j]));
            if (waveform[i + j].real())
                empty = false;
        }
        if (i > 0 && abs(waveform[i] - waveform[i - 1]) > maxdelta)
        {
            spikes++;
        }
        if (empty)
        {
            verdict = "Gaps present in the signal. Radio communication glitch?";
        }
    }
    float maxamp = 0.f;
    for (int i = 0; i < 512; ++i)
        maxamp = std::max(maxamp, waveform[i].real() / gain);

    if (maxamp < 0.03)
        verdict = "Very low signal level. Check electrode placement and excess salt on the skin.";
    
    if (spikes >= 3)
        verdict = "Phantom spikes present. Make sure the electrodes have good contact with the skin.";
    
    if (std::abs(spectrum[50]) / gain > 5)
        verdict = "Very strong 50Hz mains interference. There seems to be no contact with the skin at all.";
    else if (std::abs(spectrum[50]) / gain > 1.5)
        verdict = "50Hz mains interference. Make sure the electrodes have good contact with the skin.";
    
    if (std::abs(spectrum[60]) / gain > 1.5)
        verdict = "60Hz interference from LCD screens. Make sure the electrodes have good contact with the skin.";
    
    if (maxamp > 0.25)
        verdict = "Very high input level, likely due to noise or lack of skin contact. Check electrode placement.";
    
    return verdict;
}

#include <atomic>
#include <thread>
void push_ratio_to_clock(float frac)
{
	static union atomicAverage
	{
		struct 
		{
			int m_count;
			float m_frac;
		};
		std::atomic<long> m_storage;

		void increase(float increment)
		{
			atomicAverage newVal;
			
			long origStorage;
			do
			{
				newVal.m_storage.store(m_storage.load());
				origStorage = newVal.m_storage;
				newVal.m_count += 1;
				newVal.m_frac += increment;
			}while(!m_storage.compare_exchange_strong(origStorage, newVal.m_storage));
		}

		float retrieveAndReset()
		{
			atomicAverage self;
			atomicAverage reset;
			long loaded;
			do
			{
				self.m_storage.store(m_storage.load());
				loaded = self.m_storage.load();
				reset.m_frac = self.m_frac / float(self.m_count);
				reset.m_count = 1;
			}while(!m_storage.compare_exchange_strong(loaded, reset.m_storage));

			return self.m_frac / float(self.m_count);
		}

		atomicAverage()
		{
			m_count = 0;
			m_frac = 0.0f;
		}

	} s_data;
	s_data.increase(frac);

	std::thread s_thread([&]()
	{
		//<eoin - 5hz enough?
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		float curVal = s_data.retrieveAndReset();
		printf("\nGOT AVG %f\n", curVal);
	});
	s_thread.detach();

}

void draw(GtkWidget *dra, cairo_t *cr, gpointer user_data)
{
    SensorStateProcessor *ssp = (SensorStateProcessor *)user_data;
    SensorState ss = ssp->get_state();
    if (ss != SST_RECEIVING && ss != SST_WAITING_FOR_DATA && ss != SST_REPLAYING)
        return;
    guint width, height;
    GdkRGBA color;
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
    string verdict = virtual_chris(input, output, gain);
    
    width = gtk_widget_get_allocated_width (dra);
    height = gtk_widget_get_allocated_height (dra);

    map<string, float> sums;
    string wavetype;
    float total = 0;
    for (int i = 0; i < width; i++)
    {
        int pt = i * 100 / (width - 1);
        const char *colorspec = "rgba(0,63,127,0.5)";
        wavetype = "noise";

        // theta
        if (pt >= 1 && pt <= 3)
            colorspec = "grey", wavetype = "delta";
        // theta
        if (pt >= 4 && pt <= 7)
            colorspec = "blue", wavetype = "theta";
        // alpha
        if (pt >= 8 && pt <= 15)
            colorspec = "green", wavetype = "alpha";
        // beta
        if (pt >= 16 && pt <= 31)
            colorspec = "orange", wavetype = "beta";
        // 50 Hz or 60 Hz ground hum
        if ((pt >= 49 && pt <= 51) || (pt >= 59 && pt <= 61))
            colorspec = "red", wavetype = "hum";
        sums[wavetype] += abs(output[pt]);
        total += abs(output[pt]);
        gdk_rgba_parse(&color, colorspec);
        gdk_cairo_set_source_rgba (cr, &color);

        float ptv = log10(abs(output[pt]) + 0.00000000001) / 2;
        float pty = height * (1 - ptv);
        cairo_move_to(cr, i, height);
        cairo_line_to(cr, i, pty);
        cairo_stroke(cr);
    }
    string wavelevels;
    map<float, string> inverted;
    if (total > 0)
    {
        for(map<string, float>::const_iterator i = sums.begin(); i != sums.end(); ++i)
            inverted[i->second / total] = i->first;
        for(map<float, string>::const_iterator i = inverted.begin(); i != inverted.end(); ++i)
        {
            char buf[128];
            sprintf(buf, "%s -> %f ", i->second.c_str(), i->first);
            wavelevels += buf;
        }
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
    bool is_good = verdict.empty();
    color.red = is_good ? 0.0 : 1.0;
    color.green = is_good ? 1.0 : 0.0;
    color.blue = 0.0;
    color.alpha = ss == SST_WAITING_FOR_DATA ? 0.25 : 1.0;
    gdk_cairo_set_source_rgba (cr, &color);

    cairo_stroke (cr);    

    bool is_valid = true;
    is_valid = verdict.empty();

    if (ss == SST_WAITING_FOR_DATA)
        verdict = "Waiting for data.";
    color.red = 1.0;
    color.green = 0.5;
    color.blue = 0.0;
    color.alpha = 1.0;
    if (verdict.empty())
    {
        verdict = last_msg;
        color.alpha = 1.0 - 0.01 * last_msg_age;
        if (color.alpha < 0)
            color.alpha = 0;
        last_msg_age++;
    }
    else
    {
        last_msg = verdict;
        last_msg_age = 0;
    }
    gdk_cairo_set_source_rgba (cr, &color);
    
    cairo_move_to(cr, 20, 30);
    cairo_set_font_size(cr, 15);
    cairo_show_text(cr, verdict.c_str());

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

    if (is_valid)
    {
        color.red = 1.0;
        color.green = 1.0;
        color.blue = 1.0;
        color.alpha = 1.0;
        gdk_cairo_set_source_rgba (cr, &color);
    }

    cairo_move_to(cr, 20, 50);
    cairo_set_font_size(cr, 15);
    cairo_show_text(cr, wavelevels.c_str());
    cairo_stroke(cr);
    if (inverted.size() && is_valid)
    {
        color.red = 0.0;
        color.green = 1.0;
        color.blue = 0.0;
        color.alpha = 1.0;
        gdk_cairo_set_source_rgba (cr, &color);
        cairo_move_to(cr, 20, 80);
        cairo_set_font_size(cr, 20);
        cairo_show_text(cr, inverted.rbegin()->second.c_str()); //<eoin - rbegin on an unordered collection?
        cairo_stroke(cr);
        char buf[1000];
        sprintf(buf, "Beta:alpha = %f", sums["beta"] / sums["alpha"]);
		push_ratio_to_clock(sums["beta"] / sums["alpha"]);
        cairo_move_to(cr, 20, 110);
        cairo_set_font_size(cr, 20);
        cairo_show_text(cr, buf);
        cairo_stroke(cr);
    }
    for (int i = 0; i < 512; i += 64)
    {
        int x = i * (width - 1) / 511;
        color.alpha = 0.1;
        gdk_cairo_set_source_rgba (cr, &color);
        cairo_move_to(cr, x, height / 4);
        cairo_line_to(cr, x, 3 * height / 4);
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
    float gain = pow(2.0, gtk_adjustment_get_value(gain_adjustment) / 6.0);
    ssp->aout->set_gain(gain);
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
