#ifndef GUARD_H_JACK_CLIENT
#define GUARD_H_JACK_CLIENT

// ugly code for beautiful art :-)

void jack_append_new_data(int16_t sample);
int jack_init(int argc, char *argv[]);
void jack_run();
void jack_byebye();

#endif
