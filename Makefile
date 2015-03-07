CXXFLAGS += -I. `pkg-config --cflags glib-2.0 gtk+-3.0` -DGNU_SOURCE
LDFLAGS += -ljack -lsndfile -lm -lstdc++ `pkg-config --libs glib-2.0 gtk+-3.0` -lpthread

all: showeeg

showeeg: showeeg.o opi_linux.o sensor.o fsm.o audio_output.o sensor_thread.o

.PHONY: clean

clean:
	rm -f *.o opijack brainwave
