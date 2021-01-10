objects = dasynq-tests.o

check: dasynq-test
	./dasynq-test

$(objects): %.o: %.cc
	$(CXX) $(CXXTESTOPTS) -I../include -c $< -o $@

dasynq-test: dasynq-tests.o
	$(CXX) $(THREADOPT) $(CXXTESTLINKOPTS) dasynq-tests.o -o dasynq-test

clean:
	rm -f *.o
