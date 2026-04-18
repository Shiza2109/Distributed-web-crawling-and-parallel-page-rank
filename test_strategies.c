/*
 * test_strategies.c -
 *
 * Loads the crawled graph and runs pagerank_run_comparison() to exercise
 * all four execution strategies, printing timing and correctness results.
 
 * Defaults:
 *   graph_file  = crawled_graph_threaded.adj
 *   num_threads = 4
 *   max_iters   = 50
 *   threshold   = 1e-6
 */

#include <stdio.h>
#include <stdlib.h>
#include "pagerank.h"
#include "pagerank_strategies.h"

int main(int argc, char **argv) {
    /* --- Parse CLI arguments with sensible defaults --- */
    const char *graph_file = "crawled_graph_threaded.adj";
    int         num_threads = 4;
    int         max_iters   = 50;
    double      threshold   = 1e-6;

    if (argc > 1) graph_file  = argv[1];
    if (argc > 2) num_threads = atoi(argv[2]);
    if (argc > 3) max_iters   = atoi(argv[3]);
    if (argc > 4) threshold   = atof(argv[4]);

    if (num_threads <= 0) num_threads = 4;
    if (max_iters   <= 0) max_iters   = 50;
    if (threshold   <= 0) threshold   = 1e-6;

    printf("====================================================\n");
    printf("  PageRank Strategy Comparison  (Aleena - Milestone 2)\n");
    printf("====================================================\n");
    printf("  Graph file  : %s\n", graph_file);
    printf("  Threads     : %d\n", num_threads);
    printf("  Max iters   : %d\n", max_iters);
    printf("  Threshold   : %.0e\n", threshold);
    printf("====================================================\n\n");

    printf("[Main] Loading graph from %s ...\n", graph_file);
    PageRankGraph *pg = pagerank_load_graph(graph_file);
    if (!pg) {
        fprintf(stderr, "[Main] ERROR: Failed to load graph from %s\n", graph_file);
        return 1;
    }
    printf("[Main] Graph loaded: %d nodes\n\n", pg->num_nodes);

    pagerank_run_comparison(pg, num_threads, max_iters, threshold,
                            "strategy_comparison.txt");


    pagerank_free(pg);

    printf("[Main] Done! Results saved to strategy_comparison.txt\n");
    return 0;
}
