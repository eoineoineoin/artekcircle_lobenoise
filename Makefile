
opijack: main.o opi_linux.o jack_client.o
	$(CXX) $^ -o $@ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o opijack
