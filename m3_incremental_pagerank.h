#ifndef M3_INCREMENTAL_PAGERANK_H
#define M3_INCREMENTAL_PAGERANK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int num_nodes;
    int num_edges;
    int *out_degree;           /* Number of outlinks per node */
    int **adjacency;           /* Adjacency list */
    double *ranks;             /* Current PageRank values */
} IncrementalGraph;

typedef struct {
    int num_nodes_old;         /* Nodes in previous graph */
    int num_nodes_new;         /* Total nodes in updated graph */
    int num_new_nodes;         /* Count of newly added nodes */
    int num_affected_nodes;    /* Nodes with changed connectivity */
    uint64_t iterations;       /* Number of iterations to convergence */
    double elapsed_ms;         /* Computation time in milliseconds */
    double convergence_delta;  /* Final delta value at convergence */
    uint64_t memory_bytes;     /* Estimated memory footprint */
    uint64_t data_movement_bytes; /* Estimated rank data movement */
} RecomputationStats;

#define DAMPING_FACTOR 0.85
#define CONVERGENCE_THRESHOLD 1e-10
#define MAX_ITERATIONS 100

/* Load graph from adjacency file (.adj format) */
IncrementalGraph* incremental_load_graph(const char *filename);

/* Save ranks to output file */
void incremental_save_ranks(IncrementalGraph *g, const char *filename);

/* Print top PageRank nodes */
void incremental_print_ranks(IncrementalGraph *g, int top_k);

/* Compute full PageRank from scratch (static approach) */
RecomputationStats* incremental_pagerank_full(
    IncrementalGraph *phase2_graph,
    int num_threads
);

/* Compute incremental PageRank using Phase 1 as baseline */
RecomputationStats* incremental_pagerank_incremental(
    IncrementalGraph *phase1_graph,
    IncrementalGraph *phase2_graph,
    int num_threads
);

/* Identify nodes affected by graph changes */
int* incremental_identify_affected_nodes(
    IncrementalGraph *phase1,
    IncrementalGraph *phase2,
    int *affected_count
);

/* Compare two recomputation strategies */
typedef struct {
    RecomputationStats *full;
    RecomputationStats *incremental;
    double speedup;            /* incremental time / full time */
    double avg_rank_diff;      /* average difference between strategies */
    double max_rank_diff;      /* maximum difference between strategies */
    double l1_rank_diff;       /* L1 norm of rank difference */
    double l2_rank_diff;       /* L2 norm of rank difference */
} ComparisonResult;

ComparisonResult* incremental_compare_strategies(
    IncrementalGraph *phase1,
    IncrementalGraph *phase2,
    int num_threads
);

/* Compare strategies using file paths (avoids keeping large graphs in memory) */
ComparisonResult* incremental_compare_strategies_files(
    const char *phase1_graph_file,
    const char *phase2_graph_file,
    int num_threads
);

/* Free memory */
void incremental_free_graph(IncrementalGraph *g);
void incremental_free_stats(RecomputationStats *stats);
void incremental_free_comparison(ComparisonResult *result);

#endif
