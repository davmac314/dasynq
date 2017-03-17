CXX = g++
CXXOPTS = -std=c++11 -Os -Wall -Wno-invalid-offsetof
THREADOPT = -pthread
SANITIZE = -fsanitize=address,null,return,bounds,alignment,object-size 

export CXX CXXOPTS THREADOPT SANITIZE

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
