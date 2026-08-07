CC = g++
CPPUTEST_HOME ?= $(shell brew --prefix cpputest 2>/dev/null || echo /usr)
CFLAGS = -I.. -I./mocks -I$(CPPUTEST_HOME)/include -std=c++17 -g -Wall
LDFLAGS = -L$(CPPUTEST_HOME)/lib -lCppUTest -lCppUTestExt

all: run_tests

run_tests: AllTests.cpp wiz_client_test.cpp ../components/wiz_client/wiz_client.h
	$(CC) $(CFLAGS) -o run_tests AllTests.cpp wiz_client_test.cpp $(LDFLAGS)

test: run_tests
	./run_tests

clean:
	rm -f run_tests

.PHONY: all test clean
