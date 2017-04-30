# For Linux / Mac OS:
CXX = g++
CXXOPTS = -std=c++11 -Os -Wall -Wno-invalid-offsetof
THREADOPT = -pthread
SANITIZE = -fsanitize=address,null,return,bounds,alignment,object-size

# For OpenBSD:
#CXX = eg++
#CXXOPTS = -std=c++11 -Os -Wall -Wno-invalid-offsetof
#THREADOPT = -pthread
#SANITIZE =

# For FreeBSD:
#CXX = clang++
#CXXOPTS = -std=c++11 -Os -Wall -Wno-invalid-offsetof
#THREADOPT = -pthread
#SANITIZE =
#  Note that to use timers you'll also need to link with -lrt

export CXX CXXOPTS THREADOPT SANITIZE

all:
	@echo "This is a header-only library. Use \"$(MAKE) check\" to build and run tests."

check:
	$(MAKE) -C tests check

#install: all

#install.man:

clean:
	rm -f *.o
	$(MAKE) -C tests clean
