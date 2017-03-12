# -include ../mconfig
CXX = g++
#CXX = clang++
CXXOPTS = -std=c++11 -Wall -Wno-invalid-offsetof

objects = dasynq-tests.o

check: dasynq-test
	./dasynq-test

$(objects): %.o: %.cc
	$(CXX) -g $(CXXOPTS) -I.. -c $< -o $@

dasynq-test: dasynq-tests.o
	$(CXX) -g dasynq-tests.o -o dasynq-test

clean:
	rm -f *.o
