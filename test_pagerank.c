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
    const char *graph_file = "crawled_graph.adj";
    int num_threads = 4;  /* Default */
    
    if (argc > 1) graph_file = argv[1];
    if (argc > 2) num_threads = atoi(argv[2]);
    
    printf("========================================\n");
    printf("  PageRank Computation - Milestone 2\n");
    printf("========================================\n\n");
    
    /* Load graph */
    printf("[Main] Loading graph from %s...\n", graph_file);
    PageRankGraph *pg = pagerank_load_graph(graph_file);
    if (!pg) {
        fprintf(stderr, "Failed to load graph\n");
        return 1;
    }
    
    /* Sequential PageRank */
    printf("\n--- Sequential PageRank ---\n");
    double start = get_time_ms();
    pagerank_compute_sequential(pg);
    double seq_time = get_time_ms() - start;
    printf("Sequential time: %.2f ms\n", seq_time);
    pagerank_save_ranks(pg, "pagerank_ranks_seq.txt");
    
    /* Reset ranks for parallel run */
    double init_rank = 1.0 / pg->num_nodes;
    for (int i = 0; i < pg->num_nodes; i++) {
        pg->ranks[i] = init_rank;
    }
    
    /* Parallel PageRank */
    printf("\n--- Parallel PageRank (%d threads) ---\n", num_threads);
    start = get_time_ms();
    pagerank_compute_parallel(pg, num_threads);
    double par_time = get_time_ms() - start;
    printf("Parallel time: %.2f ms\n", par_time);
    
    /* Speedup */
    if (par_time > 0) {
        printf("Speedup: %.2fx\n", seq_time / par_time);
    }
    
    pagerank_save_ranks(pg, "pagerank_ranks_par.txt");
    pagerank_print_ranks(pg);
    
    pagerank_free(pg);
    
    printf("\n[Main] Done! Output files: pagerank_ranks_seq.txt, pagerank_ranks_par.txt\n");
    return 0;
}