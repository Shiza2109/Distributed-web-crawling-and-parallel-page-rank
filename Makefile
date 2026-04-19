# Makefile for Milestone 1 + Milestone 2
CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lpthread -lcurl
LDFLAGS_MATH = -lpthread -lm

# Source files
SOURCES = graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c manual_worker.c reproducibility.c
HEADERS = graph.h frontier.h crawl_policy.h fetch.h worker.h parser.h manual_worker.h reproducibility.h

# Test executables
TESTS = test_ibrahim.exe test_duplicate.exe test_integration.exe test_strategies.exe

all: $(TESTS)

# ---- Milestone 1 targets ----

# Your graph storage test
test_ibrahim.exe: test_ibrahim.c graph.c
	$(CC) $(CFLAGS) -o $@ test_ibrahim.c graph.c $(LDFLAGS)

# Abdur's duplicate test
test_duplicate.exe: test_duplicate.c frontier.c crawl_policy.c
	$(CC) $(CFLAGS) -o $@ test_duplicate.c frontier.c crawl_policy.c $(LDFLAGS)

# Complete integration test
test_integration.exe: test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c manual_worker.c reproducibility.c
	$(CC) $(CFLAGS) -o $@ test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c manual_worker.c reproducibility.c $(LDFLAGS)

# ---- Milestone 2 targets ----


# Aleena's strategy comparison (centralized vs distributed, fixed vs convergence)
test_strategies.exe: test_strategies.c pagerank_strategies.c pagerank.c
	$(CC) $(CFLAGS) -o $@ test_strategies.c pagerank_strategies.c pagerank.c $(LDFLAGS_MATH)

# Clean
clean:
	del /Q *.exe *.o 2>nul || rm -f *.exe *.o

# ---- Run targets ----

# Run your test
test-ibrahim: test_ibrahim.exe
	./test_ibrahim.exe

# Run duplicate test  
test-duplicate: test_duplicate.exe
	./test_duplicate.exe

# Run integration test
test-integration: test_integration.exe
	./test_integration.exe


# Run Aleena's strategy comparison (small graph)
test-strategies-small: test_strategies.exe
	./test_strategies.exe crawled_graph.adj 4 50 1e-6

# Run Aleena's strategy comparison (large crawled graph)
test-strategies: test_strategies.exe
	./test_strategies.exe crawled_graph_threaded.adj 4 50 1e-6

.PHONY: all clean test-ibrahim test-duplicate test-integration test-strategies test-strategies-small