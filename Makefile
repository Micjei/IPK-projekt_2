CXX := g++
CXXFLAGS := -std=c++17 -Wall -pthread -finput-charset=UTF-8

all: ipk24chat-server

ipk24chat-server: main.o tcp.o udp.o
	$(CXX) $(CXXFLAGS) -o $@ $^

main.o: main.cpp server.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tcp.o: tcp.cpp server.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

udp.o: udp.cpp server.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o ipk24chat-server
