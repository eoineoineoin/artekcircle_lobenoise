
// code shamelessly based upon / lifted from http://dis-dot-dat.net/index.cgi?item=jacktuts/starting/playing_a_note
// many thnx to james (at) dis-dot-dat.net

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>



// ringbuffer:
#define EGG_RINGBUFFER_SIZE (128*4)
jack_ringbuffer_t *eeg_ringbuffer;

/*one cycle of our sound*/
typedef jack_default_audio_sample_t sample_t;
sample_t* cycle;
/*samples in cycle*/
jack_nframes_t samincy;
/*the current offset*/
long offset=0;

jack_client_t *jack_client;


void jack_append_new_data(int16_t sample)
{
  //totally EVIL and error prone ... but we're at a hackaton
  int len = jack_ringbuffer_write(eeg_ringbuffer, (const char*) &sample, 2);
  assert( len == 2 || len == 0);
  if (len == 0)
    printf("Bo\r");

}


// -----------

/*Our output port*/
jack_port_t *output_port;

/*The current sample rate*/
jack_nframes_t sr;


/*frequency of our sound*/
int tone=262;

sample_t output_last_ = 0.f;

int jack_process (jack_nframes_t nframes, void *arg){
  /*grab our output buffer*/
  sample_t *out = (sample_t *) jack_port_get_buffer(output_port, nframes);


  /*For each required sample*/
  for(jack_nframes_t i=0;i < nframes;i++)
  {
    /*Copy the sample at the current position in the cycle to the buffer*/
    //totally EVIL and error prone ... but we're at a hackaton
    int16_t value = 0;
    int len = jack_ringbuffer_read(eeg_ringbuffer, (char*) &value, 2);
    assert( len == 2 || len == 0);
    if (len == 2)
    {
      output_last_ = value / 32768.f;
      out[i] = output_last_;
    }
    else
    {
      printf("Bu\r");
      out[i] = output_last_;
    }
  }
  return 0;
}

int srate (jack_nframes_t nframes, void *arg){
  printf ("the sample rate is now %u/sec\n", nframes);
  sr=nframes;
  return 0;
}

void jack_error (const char *desc){
  fprintf (stderr, "JACK error: %s\n", desc);
}

void jack_shutdown (void *arg){
  exit (1);
}

int jack_init()
{
   eeg_ringbuffer = jack_ringbuffer_create (EGG_RINGBUFFER_SIZE);

  /* tell the JACK server to call error() whenever it
     experiences an error.  Notice that this callback is
     global to this process, not specific to each client.

     This is set here so that it can catch errors in the
     connection process
  */
  jack_set_error_function (jack_error);

  /* try to become a client of the JACK server */

  if ((jack_client = jack_client_open("lobenoise",(jack_options_t)0, 0)) == 0)
  {
    fprintf (stderr, "jack server not running?\n");
    return 1;
  }

  /* tell the JACK server to call `process()' whenever
     there is work to be done.
  */

  jack_set_process_callback (jack_client, jack_process, 0);

  /* tell the JACK server to call `srate()' whenever
     the sample rate of the system changes.
  */

  jack_set_sample_rate_callback (jack_client, srate, 0);

  /* tell the JACK server to call `jack_shutdown()' if
     it ever shuts down, either entirely, or if it
     just decides to stop calling us.
  */

  jack_on_shutdown (jack_client, jack_shutdown, 0);

  ///* display the current sample rate. once the client is activated
     //(see below), you should rely on your own sample rate
     //callback (see above) for this value.
  //*/
  //printf ("engine sample rate: %u\n", jack_get_sample_rate (jack_client));


  //sr=jack_get_sample_rate (jack_client);

  /* create two ports */

  output_port = jack_port_register(jack_client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

}

int jack_run()
{
  /* tell the JACK server that we are ready to roll */
  const char **ports;

  if (jack_activate (jack_client)) {
    fprintf (stderr, "cannot activate client");
    return 1;
  }

  /* connect the ports*/
  if ((ports = jack_get_ports (jack_client, NULL, NULL,
                   JackPortIsPhysical|JackPortIsInput)) == NULL) {
    fprintf(stderr, "Cannot find any physical playback ports\n");
    exit(91);
  }

  int i=0;
  while(ports[i]!=NULL){
    if (jack_connect (jack_client, jack_port_name (output_port), ports[i])) {
      fprintf (stderr, "cannot connect output ports\n");
    }
    i++;
  }

  free (ports);
  return 0;
}

void jack_byebye()
{
  jack_client_close (jack_client);
  jack_ringbuffer_free(eeg_ringbuffer);
}

