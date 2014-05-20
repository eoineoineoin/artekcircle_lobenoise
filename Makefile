CXXFLAGS += -I. `pkg-config --cflags glib-2.0 gtk+-3.0`
LDFLAGS += -ljack -lsndfile -lm -lstdc++ `pkg-config --libs glib-2.0 gtk+-3.0`

all: opijack brainwave

brainwave: brainwave.o opi_linux.o

opijack: main.o opi_linux.o jack_client.o
	$(CXX) $^ -o $@ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f *.o opijack brainwave
