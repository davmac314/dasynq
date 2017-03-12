# -include ../mconfig
CXX = g++
#CXX = clang++
CXXOPTS = -std=c++11 -Wall -Wno-invalid-offsetof

objects = dasynq.o

all: dasynq.o

$(objects): %.o: %.cc   dasynq.h
	$(CXX) $(CXXOPTS) -c $< -o $@

check:
	$(MAKE) -C tests check

#install: all

#install.man:

clean:
	rm -f *.o
	$(MAKE) -C tests clean
