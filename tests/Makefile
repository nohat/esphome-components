CC = g++
CFLAGS = -I../components -I/usr/local/include -g -Wall
LDFLAGS = -L/usr/local/lib -lCppUTest -lCppUTestExt

all: run_tests

run_tests: AllTests.cpp wiz_client_test.cpp
	$(CC) $(CFLAGS) -o run_tests AllTests.cpp wiz_client_test.cpp $(LDFLAGS)

clean:
	rm -f run_tests