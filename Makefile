CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -g -pthread

all: jms_coord jms_console

jms_coord: jms_coord.o protocol.o job_manager.o
	$(CXX) $(CXXFLAGS) -o jms_coord jms_coord.o protocol.o job_manager.o

jms_coord.o: jms_coord.cpp
	$(CXX) $(CXXFLAGS) -c jms_coord.cpp

jms_console: jms_console.o protocol.o
	$(CXX) $(CXXFLAGS) -o jms_console jms_console.o protocol.o

jms_console.o: jms_console.cpp
	$(CXX) $(CXXFLAGS) -c jms_console.cpp

protocol.o: protocol.cpp
	$(CXX) $(CXXFLAGS) -c protocol.cpp

job_manager.o: job_manager.cpp
	$(CXX) $(CXXFLAGS) -c job_manager.cpp

clean:
	rm -f *.o jms_coord jms_console