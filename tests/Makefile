# -include ../mconfig
CXX = g++
#CXX = clang++
CXXOPTS = -Os -std=c++11 -Wall -Wno-invalid-offsetof -fsanitize=address,null,return,bounds,alignment,object-size

objects = dasynq-tests.o

check: dasynq-test
	./dasynq-test

$(objects): %.o: %.cc
	$(CXX) $(CXXOPTS) -I.. -c $< -o $@

dasynq-test: dasynq-tests.o
	$(CXX) $(CXXOPTS) dasynq-tests.o -o dasynq-test

clean:
	rm -f *.o
