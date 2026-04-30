/*
 * pagerank_strategies.h 
 *
 * Implements four PageRank execution strategies combining two axes:
 *   Aggregation : Centralized (shared array + per-node mutex)
 *                 vs Distributed (thread-local buffers + reduction)
 *   Termination : Fixed iteration count vs convergence-based (L1 norm)
 
 */
#ifndef PAGERANK_STRATEGIES_H
#define PAGERANK_STRATEGIES_H

#include "pagerank.h"

   // Result struct returned by every strategy run
typedef struct {
    double   elapsed_ms;       
    int      iterations;       
    double   final_diff;       
} StrategyResult;

   // Strategy 1: Centralized Aggregation + Fixed Iterations

StrategyResult pagerank_centralized_fixed(PageRankGraph *pg,
                                          int num_threads,
                                          int max_iters);

   // Strategy 2: Centralized Aggregation + Convergence Termination
   // - Workers write to a shared new_ranks[] via per-node mutexes
   // - Stops when L1 diff < threshold (or MAX_ITERATIONS as safety cap)

StrategyResult pagerank_centralized_convergence(PageRankGraph *pg,
                                                int num_threads,
                                                double threshold);

   // Strategy 3: Distributed Reduction + Fixed Iterations
   // - Each worker keeps a private local buffer (no mutexes during compute)
   // - Main thread reduces (sums) all local buffers after each iteration
   // - Runs exactly max_iters iterations, ignores convergence
StrategyResult pagerank_distributed_fixed(PageRankGraph *pg,
                                          int num_threads,
                                          int max_iters);

   // Strategy 4: Distributed Reduction + Convergence Termination
   // - Each worker keeps a private local buffer (no mutexes during compute)
   // - Main thread reduces (sums) all local buffers after each iteration
   // - Stops when L1 diff < threshold (or MAX_ITERATIONS as safety cap)
StrategyResult pagerank_distributed_convergence(PageRankGraph *pg,
                                                int num_threads,
                                                double threshold);

   // Benchmark driver — runs all four strategies, prints comparison
void pagerank_run_comparison(PageRankGraph *pg,
                             int num_threads,
                             int max_iters,
                             double threshold,
                             const char *output_file);

#endif /* PAGERANK_STRATEGIES_H */
