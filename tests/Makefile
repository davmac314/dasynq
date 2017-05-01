objects = dasynq-tests.o

check: dasynq-test
	./dasynq-test

$(objects): %.o: %.cc
	$(CXX) $(CXXOPTS) $(CXXTESTOPTS) $(SANITIZE) -I.. -c $< -o $@

dasynq-test: dasynq-tests.o
	$(CXX) $(THREADOPT) $(CXXLINKOPTS) $(SANITIZE) dasynq-tests.o -o dasynq-test

clean:
	rm -f *.o
