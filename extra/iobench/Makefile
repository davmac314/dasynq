all: evbench dbench

evbench: bench.c
	gcc -Ilibev -O3 bench.c -o evbench

dbench: bench.cc
	g++ -O3 bench.cc -I../../include -o dbench
