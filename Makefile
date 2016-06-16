# -include ../mconfig
CXX = g++
#CXX = clang++
CXXOPTS = -std=c++11 -Wall -Wno-invalid-offsetof

objects = dasynq.o testme.o

all: dasynq-test

$(objects): %.o: %.cc   dasynq.h
	$(CXX) $(CXXOPTS) -c $< -o $@

dasynq-test: dasynq.o testme.o
	$(CXX) dasynq.o testme.o -o dasynq-test

#install: all

#install.man:

clean:
	rm -f *.o
