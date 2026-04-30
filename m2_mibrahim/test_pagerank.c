
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "pagerank.h"

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    return (double)GetTickCount64();
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

int main(int argc, char **argv) {
    const char *graph_file = "graph.adj";
    
    if (argc > 1) graph_file = argv[1];
    
    printf("========================================\n");
    printf("  Sequential PageRank - Milestone 2\n");
    printf("========================================\n\n");
    
    printf("[Main] Loading graph from %s...\n", graph_file);
    fflush(stdout);
    
    PageRankGraph *pg = pagerank_load_graph(graph_file);
    if (!pg) {
        fprintf(stderr, "Failed to load graph\n");
        return 1;
    }
    
    printf("\n--- Running PageRank ---\n");
    double start = get_time_ms();
    pagerank_compute(pg);
    double elapsed = get_time_ms() - start;
    
    printf("\nTotal time: %.2f ms (%.2f seconds)\n", elapsed, elapsed / 1000);
    
    /* Save results */
    pagerank_save_ranks(pg, "pagerank_ranks.txt");
    
    /* Print first few ranks */
    pagerank_print_ranks(pg);
    
    pagerank_free(pg);
    
    printf("\n[Main] Done! Output saved to pagerank_ranks.txt\n");
    return 0;
}