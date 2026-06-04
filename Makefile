CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -g

all: jms_coord jms_console

jms_coord: jms_coord.o
	$(CXX) $(CXXFLAGS) -o jms_coord jms_coord.o

jms_coord.o: jms_coord.cpp
	$(CXX) $(CXXFLAGS) -c jms_coord.cpp

jms_console: jms_console.o
	$(CXX) $(CXXFLAGS) -o jms_console jms_console.o

jms_console.o: jms_console.cpp
	$(CXX) $(CXXFLAGS) -c jms_console.cpp

clean:
	rm -f *.o jms_coord jms_console
