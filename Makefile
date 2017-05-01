# Installation:
#DESTDIR=  (acts as prefix to other paths for installation)
HEADERDIR=/usr/include/dasynq
LIBDIR=/usr/lib

# For Linux / Mac OS:
CXX = g++
CXXOPTS = 
SANITIZE = -fsanitize=address,null,return,bounds,alignment,object-size
CXXTESTOPTS = -std=c++11 -Os -Wall $(SANITIZE)
CXXLINKOPTS =
THREADOPT = -pthread

# For OpenBSD:
#CXX = eg++
#CXXOPTS =
#SANITIZE =
#CXXTESTOPTS = -std=c++11 -Os -Wall $(SANITIZE)
#CXXLINKOPTS =
#THREADOPT = -pthread

# For FreeBSD:
#CXX = clang++
#CXXOPTS =
#SANITIZE = address,null,return,bounds,alignment,object-size
#CXXTESTOPTS = -std=c++11 -Os -Wall $(SANITIZE)
#CXXLINKOPTS = -lrt
#THREADOPT = -pthread

export CXX CXXOPTS CXXLINKOPTS CXXTESTOPTS THREADOPT SANITIZE

all:
	@echo "This is a header-only library. Use \"$(MAKE) check\" to build and run tests."

check:
	$(MAKE) -C tests check

# pkg-config file:
dasynq.pc:
	@echo "Writing dasynq.pc file."
	@rm -f dasynq.pc
	@echo "# Dasynq - event loop library" >> dasynq.pc
	@echo "Name: Dasynq" >> dasynq.pc
	@echo "Description:  Event-loop library, C++" >> dasynq.pc
	@echo "Version: 0.9" >> dasynq.pc
	@echo "URL: http://github.com/davmac314/dasynq" >> dasynq.pc
	@echo "Libs: $(CXXLINKOPT)" >> dasynq.pc
	@echo "Cflags: $(CXXOPTS) -I$(HEADERDIR)" >> dasynq.pc

install: dasynq.pc
	install -d $(DESTDIR)$(HEADERDIR) $(DESTDIR)$(LIBDIR)/pkgconfig
	install -m644 -t $(DESTDIR)$(HEADERDIR) dasynq-*.h
	install -m644 -t $(DESTDIR)$(LIBDIR)/pkgconfig dasynq.pc

clean:
	rm -f *.o
	$(MAKE) -C tests clean
