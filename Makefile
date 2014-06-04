CXXFLAGS += -I. `pkg-config --cflags glib-2.0 gtk+-3.0` -DGNU_SOURCE
LDFLAGS += -ljack -lsndfile -lm -lstdc++ `pkg-config --libs glib-2.0 gtk+-3.0` -lpthread

all: opijack brainwave showeeg

brainwave: brainwave.o opi_linux.o sensor.o fsm.o

showeeg: showeeg.o opi_linux.o sensor.o fsm.o

opijack: main.o opi_linux.o jack_client.o sensor.o
	$(CXX) $^ -o $@ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f *.o opijack brainwave
