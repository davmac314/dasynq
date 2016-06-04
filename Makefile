# -include ../mconfig
#CXX = g++
CXX = clang++
CXXOPTS = -std=c++11 -Wall -Wno-invalid-offsetof

objects = dasync.o testme.o

all: dasync-test

$(objects): %.o: %.cc   dasync.h
	$(CXX) $(CXXOPTS) -c $< -o $@

dasync-test: dasync.o testme.o
	$(CXX) dasync.o testme.o -o dasync-test

#install: all

#install.man:

clean:
	rm -f *.o
