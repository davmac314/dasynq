all: evbench dbench

evbench: bench.c
	gcc -O3 bench.c -o evbench -lev

dbench: bench.cc
	g++ -O3 bench.cc -I../.. -o dbench
