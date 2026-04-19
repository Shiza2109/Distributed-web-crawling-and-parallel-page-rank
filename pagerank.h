#ifndef PAGERANK_H
#define PAGERANK_H

#include <stdint.h>
#include <stdbool.h>

#define DAMPING_FACTOR 0.85
#define MAX_ITERATIONS 100
#define CONVERGENCE_THRESHOLD 1e-8

typedef struct {
    int num_nodes;
    int *outlink_counts;     /* outdegree for each node */
    int **outlinks;          /* adjacency list: outlinks[node_id] = array of neighbors */
    double *ranks;           /* current PageRank scores */
    double *new_ranks;       /* new ranks during iteration */
} PageRankGraph;

/* Load graph from adjacency file (crawled_graph.adj format) */
PageRankGraph* pagerank_load_graph(const char *filename);

/* Sequential PageRank implementation */
void pagerank_compute_sequential(PageRankGraph *pg);

/* Parallel PageRank implementation with N threads */
void pagerank_compute_parallel(PageRankGraph *pg, int num_threads);

/* Save ranks to output file */
void pagerank_save_ranks(PageRankGraph *pg, const char *filename);

/* Print ranks to console */
void pagerank_print_ranks(PageRankGraph *pg);

/* Free graph memory */
void pagerank_free(PageRankGraph *pg);

#endif