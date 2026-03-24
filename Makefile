# Makefile for Milestone 1
CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lpthread -lcurl

# Source files
SOURCES = graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c
HEADERS = graph.h frontier.h crawl_policy.h fetch.h worker.h parser.h

# Test executables
TESTS = test_ibrahim.exe test_duplicate.exe test_integration.exe

all: $(TESTS)

# Your graph storage test
test_ibrahim.exe: test_ibrahim.c graph.c
	$(CC) $(CFLAGS) -o $@ test_ibrahim.c graph.c $(LDFLAGS)

# Abdur's duplicate test
test_duplicate.exe: test_duplicate.c frontier.c crawl_policy.c
	$(CC) $(CFLAGS) -o $@ test_duplicate.c frontier.c crawl_policy.c $(LDFLAGS)

# Complete integration test
test_integration.exe: test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c
	$(CC) $(CFLAGS) -o $@ test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c $(LDFLAGS)

# Clean
clean:
	del /Q *.exe *.o *.adj *.edge *.txt 2>nul || rm -f *.exe *.o *.adj *.edge *.txt

# Run your test
test-ibrahim: test_ibrahim.exe
	./test_ibrahim.exe

# Run duplicate test  
test-duplicate: test_duplicate.exe
	./test_duplicate.exe

# Run integration test
test-integration: test_integration.exe
	./test_integration.exe

.PHONY: all clean test-ibrahim test-duplicate test-integration