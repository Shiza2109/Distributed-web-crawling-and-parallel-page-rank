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

test_ibrahim.exe: test_ibrahim.c graph.c
	$(CC) $(CFLAGS) -o $@ test_ibrahim.c graph.c $(LDFLAGS)

test_duplicate.exe: test_duplicate.c frontier.c crawl_policy.c
	$(CC) $(CFLAGS) -o $@ test_duplicate.c frontier.c crawl_policy.c $(LDFLAGS)

test_integration.exe: test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c manual_worker.c reproducibility.c
	$(CC) $(CFLAGS) -o $@ test_integration.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c manual_worker.c reproducibility.c $(LDFLAGS)

# ---- Milestone 2 targets ----

test_strategies.exe: test_strategies.c pagerank_strategies.c pagerank.c
	$(CC) $(CFLAGS) -o $@ test_strategies.c pagerank_strategies.c pagerank.c $(LDFLAGS_MATH)

# graph500-22 benchmark (undirected)
benchmark_graph500.exe: benchmark_graph500.c pagerank_strategies.c pagerank.c
	$(CC) $(CFLAGS) -O2 -o $@ benchmark_graph500.c pagerank_strategies.c pagerank.c $(LDFLAGS_MATH)

# wiki-Talk + cit-Patents benchmark (directed)
benchmark_small.exe: benchmark_small_datasets.c pagerank_strategies.c pagerank.c
	$(CC) $(CFLAGS) -O2 -o $@ benchmark_small_datasets.c pagerank_strategies.c pagerank.c $(LDFLAGS_MATH)

# ---- Milestone 3 targets (Aleena) ----

m3_incremental.exe: m3_incremental.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c pagerank.c
	$(CC) $(CFLAGS) -o $@ m3_incremental.c graph.c frontier.c crawl_policy.c fetch.c worker.c parser.c pagerank.c $(LDFLAGS) -lm

# Clean
clean:
	del /Q *.exe *.o 2>nul || rm -f *.exe *.o

# ---- Run targets ----

test-ibrahim: test_ibrahim.exe
	./test_ibrahim.exe

test-duplicate: test_duplicate.exe
	./test_duplicate.exe

test-integration: test_integration.exe
	./test_integration.exe

test-strategies-small: test_strategies.exe
	./test_strategies.exe crawled_graph.adj 4 50 1e-6

test-strategies: test_strategies.exe
	./test_strategies.exe crawled_graph_threaded.adj 4 50 1e-6

benchmark-graph500: benchmark_graph500.exe
	./benchmark_graph500.exe graph500-22/graph500-22.e 4 10 1e-4

benchmark-graph500-custom: benchmark_graph500.exe
	./benchmark_graph500.exe graph500-22/graph500-22.e $(T) $(I) $(THR)

benchmark-wiki: benchmark_small.exe
	./benchmark_small.exe wiki-Talk/wiki-Talk.e wiki-Talk 4 10 1e-4

benchmark-patents: benchmark_small.exe
	./benchmark_small.exe citi-patents/cit-Patents.e cit-Patents 4 10 1e-4

run-m3: m3_incremental.exe
	./m3_incremental.exe --threads 4 --depth 0 --max-pages 100

.PHONY: all clean test-ibrahim test-duplicate test-integration \
        test-strategies test-strategies-small \
        benchmark-graph500 benchmark-graph500-custom \
        benchmark-wiki benchmark-patents \
        run-m3
