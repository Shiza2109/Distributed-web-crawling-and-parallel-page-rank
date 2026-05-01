#ifndef PARALLEL_PAGERANK_H
#define PARALLEL_PAGERANK_H

#include <pthread.h>

typedef struct {
    int num_nodes;
    int num_edges;
    int *out_degree;           /* Number of outlinks per node */
    int **adjacency;           /* Adjacency list */
    double *current_rank;      /* Current PageRank values */
    double *next_rank;         /* Next iteration PageRank values */
    pthread_mutex_t *rank_locks;  /* Locks for rank updates */
    int num_threads;           /* Number of threads for computation */
} ParallelGraph;

#define DAMPING_FACTOR 0.85
#define CONVERGENCE_THRESHOLD 1e-10

/* Allocate a graph structure */
ParallelGraph* parallel_allocate_graph(int num_nodes);

/* Load graph from adjacency file (.adj format) */
ParallelGraph* parallel_load_graph(const char *filename);

/* Compute parallel PageRank with specified number of threads */
void parallel_pagerank_compute(ParallelGraph *g, int num_threads);

/* Save ranks to output file */
void parallel_pagerank_save_ranks(ParallelGraph *g, const char *filename);

/* Print ranks to console */
void parallel_pagerank_print_ranks(ParallelGraph *g);

/* Free graph memory */
void parallel_free_graph(ParallelGraph *g);

#endif
