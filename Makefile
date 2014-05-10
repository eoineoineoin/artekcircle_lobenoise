
opijack: main.o opi_linux.o
	$(CXX) $^ -o $@ $(LIBS)

.PHONY: clean

clean:
	rm -f *.o opijack
