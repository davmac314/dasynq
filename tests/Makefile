objects = dasynq-tests.o

check: dasynq-test
	./dasynq-test

$(objects): %.o: %.cc
	$(CXX) $(CXXOPTS) $(SANITIZE) -I.. -c $< -o $@

dasynq-test: dasynq-tests.o
	$(CXX) $(THREADOPT) $(CXXOPTS) $(SANITIZE) dasynq-tests.o -o dasynq-test

clean:
	rm -f *.o
