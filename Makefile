CXX = g++
CXXOPTS = -std=c++11 -Os -Wall -Wno-invalid-offsetof
THREADOPT = -pthread
SANITIZE = -fsanitize=address,null,return,bounds,alignment,object-size 

export CXX CXXOPTS THREADOPT SANITIZE

all:
	@echo "This is a header-only library. Use \"make check\" to build and run tests."

check:
	$(MAKE) -C tests check

#install: all

#install.man:

clean:
	rm -f *.o
	$(MAKE) -C tests clean
