/*
 * pagerank_strategies.c 
 *
 * Four parallel PageRank strategies that combine two orthogonal axes:
 *
 *   AGGREGATION
 *     Centralized : all threads write to ONE shared new_ranks[] array
 *                   protected by per-node mutexes.
 *     Distributed : each thread writes to its OWN local_ranks[] buffer;
 *                   the main thread reduces (sums) them after computation.
 */

#include "pagerank_strategies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <float.h>

#ifdef _WIN32
#include <windows.h>
static double timer_ms(void) { return (double)GetTickCount64(); }
#else
#include <sys/time.h>
static double timer_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

/* Re-initialize ranks to 1/N before each strategy run */
static void reset_ranks(PageRankGraph *pg) {
    int N = pg->num_nodes;
    double init = 1.0 / N;
    for (int i = 0; i < N; i++) {
        pg->ranks[i]     = init;
        pg->new_ranks[i] = 0.0;
    }
}

/* Compute L1 diff between new_ranks and ranks, then copy new -> current */
static double swap_and_diff(PageRankGraph *pg) {
    int N = pg->num_nodes;
    double diff = 0.0;
    for (int i = 0; i < N; i++) {
        diff += fabs(pg->new_ranks[i] - pg->ranks[i]);
        pg->ranks[i] = pg->new_ranks[i];
    }
    return diff;
}

//    CENTRALIZED AGGREGATION — thread args & workers
//    All threads share pg->new_ranks[] and use per-node mutexes.

typedef struct {
    PageRankGraph    *pg;
    int               start_node;
    int               end_node;
    double            teleport;
    double            damping;
    int               N;
    pthread_mutex_t  *node_mutexes;
} CentralizedArgs;

/* Reset new_ranks for the assigned slice */
static void *central_reset_worker(void *arg) {
    CentralizedArgs *a = (CentralizedArgs *)arg;
    for (int i = a->start_node; i < a->end_node; i++) {
        a->pg->new_ranks[i] = a->teleport;
    }
    return NULL;
}

/* Distribute rank contributions to shared new_ranks[] via mutexes */
static void *central_distribute_worker(void *arg) {
    CentralizedArgs *a = (CentralizedArgs *)arg;
    PageRankGraph *pg  = a->pg;
    int N              = a->N;
    double damping     = a->damping;

    for (int i = a->start_node; i < a->end_node; i++) {
        double rank_contrib = damping * pg->ranks[i];

        if (pg->outlink_counts[i] > 0) {
            double share = rank_contrib / pg->outlink_counts[i];
            for (int j = 0; j < pg->outlink_counts[i]; j++) {
                int target = pg->outlinks[i][j];
                pthread_mutex_lock(&a->node_mutexes[target]);
                pg->new_ranks[target] += share;
                pthread_mutex_unlock(&a->node_mutexes[target]);
            }
        } else {
            /* Dangling page: distribute equally to all */
            double share = rank_contrib / N;
            for (int j = 0; j < N; j++) {
                pthread_mutex_lock(&a->node_mutexes[j]);
                pg->new_ranks[j] += share;
                pthread_mutex_unlock(&a->node_mutexes[j]);
            }
        }
    }
    return NULL;
}

/* Run one centralized iteration (reset + distribute).
   Returns after all threads join. */
static void centralized_one_iteration(PageRankGraph *pg, int num_threads,
                                      double damping, double teleport,
                                      pthread_mutex_t *node_mutexes) {
    int N = pg->num_nodes;
    int nodes_per_thread = (N + num_threads - 1) / num_threads;

    pthread_t       *threads = malloc(num_threads * sizeof(pthread_t));
    CentralizedArgs *args    = malloc(num_threads * sizeof(CentralizedArgs));
    int active = 0;

    /* Phase 1: reset */
    for (int t = 0; t < num_threads; t++) {
        int s = t * nodes_per_thread;
        int e = (t + 1) * nodes_per_thread;
        if (s >= N) break;
        if (e > N)  e = N;

        args[t] = (CentralizedArgs){ pg, s, e, teleport, damping, N, node_mutexes };
        pthread_create(&threads[t], NULL, central_reset_worker, &args[t]);
        active++;
    }
    for (int t = 0; t < active; t++) pthread_join(threads[t], NULL);

    /* Phase 2: distribute */
    active = 0;
    for (int t = 0; t < num_threads; t++) {
        int s = t * nodes_per_thread;
        int e = (t + 1) * nodes_per_thread;
        if (s >= N) break;
        if (e > N)  e = N;

        args[t] = (CentralizedArgs){ pg, s, e, teleport, damping, N, node_mutexes };
        pthread_create(&threads[t], NULL, central_distribute_worker, &args[t]);
        active++;
    }
    for (int t = 0; t < active; t++) pthread_join(threads[t], NULL);

    free(threads);
    free(args);
}

//    DISTRIBUTED REDUCTION — thread args & workers
//    Each thread gets its own local_new_ranks[] buffer.
//    No mutexes during computation — reduction happens after join.

typedef struct {
    PageRankGraph *pg;
    int            start_node;
    int            end_node;
    double         teleport;
    double         damping;
    int            N;
    double        *local_new_ranks;   
} DistributedArgs;

/*
 * Each thread:
 *   1) Initializes its local buffer to 0 (teleport added at reduction)
 *   2) For each source node in [start, end), distributes rank
 *      contributions into its OWN local_new_ranks[].
 *
 * Because each thread writes only to its private buffer, there are
 * NO mutexes or atomics during this phase — the key advantage of
 * distributed reduction over centralized aggregation.
 */
static void *distributed_compute_worker(void *arg) {
    DistributedArgs *a = (DistributedArgs *)arg;
    PageRankGraph *pg  = a->pg;
    int N              = a->N;
    double damping     = a->damping;

    /* Zero the local buffer */
    memset(a->local_new_ranks, 0, N * sizeof(double));

    for (int i = a->start_node; i < a->end_node; i++) {
        double rank_contrib = damping * pg->ranks[i];

        if (pg->outlink_counts[i] > 0) {
            double share = rank_contrib / pg->outlink_counts[i];
            for (int j = 0; j < pg->outlink_counts[i]; j++) {
                int target = pg->outlinks[i][j];
                a->local_new_ranks[target] += share;
            }
        } else {
            /* Dangling page: distribute equally */
            double share = rank_contrib / N;
            for (int j = 0; j < N; j++) {
                a->local_new_ranks[j] += share;
            }
        }
    }
    return NULL;
}

/*
 * Run one distributed-reduction iteration:
 *   1) Each thread computes into its private buffer (parallel, no locks)
 *   2) Main thread reduces all buffers and adds teleport component
 */
static void distributed_one_iteration(PageRankGraph *pg, int num_threads,
                                      double damping, double teleport,
                                      double **local_buffers) {
    int N = pg->num_nodes;
    int nodes_per_thread = (N + num_threads - 1) / num_threads;

    pthread_t        *threads = malloc(num_threads * sizeof(pthread_t));
    DistributedArgs  *args    = malloc(num_threads * sizeof(DistributedArgs));
    int active = 0;

    /* Launch workers — each writes to its own local_buffers[t] */
    for (int t = 0; t < num_threads; t++) {
        int s = t * nodes_per_thread;
        int e = (t + 1) * nodes_per_thread;
        if (s >= N) break;
        if (e > N)  e = N;

        args[t] = (DistributedArgs){ pg, s, e, teleport, damping, N, local_buffers[t] };
        pthread_create(&threads[t], NULL, distributed_compute_worker, &args[t]);
        active++;
    }
    for (int t = 0; t < active; t++) pthread_join(threads[t], NULL);

    /* Reduction: sum all local buffers + teleport into pg->new_ranks[] */
    for (int i = 0; i < N; i++) {
        double sum = teleport;   /* start with teleport component */
        for (int t = 0; t < active; t++) {
            sum += local_buffers[t][i];
        }
        pg->new_ranks[i] = sum;
    }

    free(threads);
    free(args);
}

/* ================================================================
   STRATEGY 1: Centralized + Fixed Iterations
   ================================================================ */

StrategyResult pagerank_centralized_fixed(PageRankGraph *pg,
                                          int num_threads,
                                          int max_iters) {
    StrategyResult res = {0};
    if (!pg || num_threads <= 0 || max_iters <= 0) return res;

    int N = pg->num_nodes;
    double damping  = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;

    reset_ranks(pg);

    /* Allocate per-node mutexes */
    pthread_mutex_t *node_mutexes = malloc(N * sizeof(pthread_mutex_t));
    for (int i = 0; i < N; i++) pthread_mutex_init(&node_mutexes[i], NULL);

    printf("  [Centralized + Fixed] %d threads, %d iterations\n",
           num_threads, max_iters);

    double start = timer_ms();

    double diff = 0.0;
    for (int iter = 0; iter < max_iters; iter++) {
        centralized_one_iteration(pg, num_threads, damping, teleport, node_mutexes);
        diff = swap_and_diff(pg);

        if ((iter + 1) % 10 == 0 || iter == max_iters - 1) {
            printf("    Iter %3d: L1 diff = %.6e\n", iter + 1, diff);
        }
    }

    res.elapsed_ms  = timer_ms() - start;
    res.iterations  = max_iters;
    res.final_diff  = diff;

    /* Cleanup */
    for (int i = 0; i < N; i++) pthread_mutex_destroy(&node_mutexes[i]);
    free(node_mutexes);

    printf("  [Centralized + Fixed] Done: %.2f ms\n", res.elapsed_ms);
    return res;
}

/* ================================================================
   STRATEGY 2: Centralized + Convergence
   ================================================================ */

StrategyResult pagerank_centralized_convergence(PageRankGraph *pg,
                                                int num_threads,
                                                double threshold) {
    StrategyResult res = {0};
    if (!pg || num_threads <= 0 || threshold <= 0.0) return res;

    int N = pg->num_nodes;
    double damping  = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;

    reset_ranks(pg);

    pthread_mutex_t *node_mutexes = malloc(N * sizeof(pthread_mutex_t));
    for (int i = 0; i < N; i++) pthread_mutex_init(&node_mutexes[i], NULL);

    printf("  [Centralized + Convergence] %d threads, threshold=%.0e\n",
           num_threads, threshold);

    double start = timer_ms();

    int iter;
    double diff = 0.0;
    for (iter = 0; iter < MAX_ITERATIONS; iter++) {
        centralized_one_iteration(pg, num_threads, damping, teleport, node_mutexes);
        diff = swap_and_diff(pg);

        if ((iter + 1) % 10 == 0) {
            printf("    Iter %3d: L1 diff = %.6e\n", iter + 1, diff);
        }

        if (diff < threshold) {
            printf("    Converged at iteration %d (diff=%.6e)\n", iter + 1, diff);
            break;
        }
    }

    res.elapsed_ms  = timer_ms() - start;
    res.iterations  = iter + 1;
    res.final_diff  = diff;

    for (int i = 0; i < N; i++) pthread_mutex_destroy(&node_mutexes[i]);
    free(node_mutexes);

    printf("  [Centralized + Convergence] Done: %.2f ms, %d iters\n",
           res.elapsed_ms, res.iterations);
    return res;
}

/* ================================================================
   STRATEGY 3: Distributed + Fixed Iterations
   ================================================================ */

StrategyResult pagerank_distributed_fixed(PageRankGraph *pg,
                                          int num_threads,
                                          int max_iters) {
    StrategyResult res = {0};
    if (!pg || num_threads <= 0 || max_iters <= 0) return res;

    int N = pg->num_nodes;
    double damping  = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;

    reset_ranks(pg);

    /* Allocate per-thread local buffers */
    double **local_buffers = malloc(num_threads * sizeof(double *));
    for (int t = 0; t < num_threads; t++) {
        local_buffers[t] = malloc(N * sizeof(double));
    }

    printf("  [Distributed + Fixed] %d threads, %d iterations\n",
           num_threads, max_iters);

    double start = timer_ms();

    double diff = 0.0;
    for (int iter = 0; iter < max_iters; iter++) {
        distributed_one_iteration(pg, num_threads, damping, teleport, local_buffers);
        diff = swap_and_diff(pg);

        if ((iter + 1) % 10 == 0 || iter == max_iters - 1) {
            printf("    Iter %3d: L1 diff = %.6e\n", iter + 1, diff);
        }
    }

    res.elapsed_ms  = timer_ms() - start;
    res.iterations  = max_iters;
    res.final_diff  = diff;

    for (int t = 0; t < num_threads; t++) free(local_buffers[t]);
    free(local_buffers);

    printf("  [Distributed + Fixed] Done: %.2f ms\n", res.elapsed_ms);
    return res;
}

/* ================================================================
   STRATEGY 4: Distributed + Convergence
   ================================================================ */

StrategyResult pagerank_distributed_convergence(PageRankGraph *pg,
                                                int num_threads,
                                                double threshold) {
    StrategyResult res = {0};
    if (!pg || num_threads <= 0 || threshold <= 0.0) return res;

    int N = pg->num_nodes;
    double damping  = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;

    reset_ranks(pg);

    double **local_buffers = malloc(num_threads * sizeof(double *));
    for (int t = 0; t < num_threads; t++) {
        local_buffers[t] = malloc(N * sizeof(double));
    }

    printf("  [Distributed + Convergence] %d threads, threshold=%.0e\n",
           num_threads, threshold);

    double start = timer_ms();

    int iter;
    double diff = 0.0;
    for (iter = 0; iter < MAX_ITERATIONS; iter++) {
        distributed_one_iteration(pg, num_threads, damping, teleport, local_buffers);
        diff = swap_and_diff(pg);

        if ((iter + 1) % 10 == 0) {
            printf("    Iter %3d: L1 diff = %.6e\n", iter + 1, diff);
        }

        if (diff < threshold) {
            printf("    Converged at iteration %d (diff=%.6e)\n", iter + 1, diff);
            break;
        }
    }

    res.elapsed_ms  = timer_ms() - start;
    res.iterations  = iter + 1;
    res.final_diff  = diff;

    for (int t = 0; t < num_threads; t++) free(local_buffers[t]);
    free(local_buffers);

    printf("  [Distributed + Convergence] Done: %.2f ms, %d iters\n",
           res.elapsed_ms, res.iterations);
    return res;
}

/* ================================================================
   Comparison driver — runs all four, prints a summary table
   ================================================================ */

/* Helper: find top-K nodes by rank */
static void find_top_nodes(PageRankGraph *pg, int k, int *top_ids, double *top_ranks) {
    int N = pg->num_nodes;
    if (k > N) k = N;

    /* Initialize with first k nodes */
    for (int i = 0; i < k; i++) {
        top_ids[i]   = i;
        top_ranks[i] = pg->ranks[i];
    }

    /* Selection: for each remaining node, replace smallest in top-k if bigger */
    for (int i = k; i < N; i++) {
        /* Find min in current top-k */
        int min_idx = 0;
        for (int j = 1; j < k; j++) {
            if (top_ranks[j] < top_ranks[min_idx]) min_idx = j;
        }
        if (pg->ranks[i] > top_ranks[min_idx]) {
            top_ids[min_idx]   = i;
            top_ranks[min_idx] = pg->ranks[i];
        }
    }

    /* Sort top-k descending by rank (simple bubble for small k) */
    for (int i = 0; i < k - 1; i++) {
        for (int j = i + 1; j < k; j++) {
            if (top_ranks[j] > top_ranks[i]) {
                double tr   = top_ranks[i]; top_ranks[i] = top_ranks[j]; top_ranks[j] = tr;
                int    ti   = top_ids[i];   top_ids[i]   = top_ids[j];   top_ids[j]   = ti;
            }
        }
    }
}

void pagerank_run_comparison(PageRankGraph *pg,
                             int num_threads,
                             int max_iters,
                             double threshold,
                             const char *output_file) {
    if (!pg) return;

    const int TOP_K = 5;
    int    top_ids[4][5];
    double top_ranks[4][5];

    const char *names[4] = {
        "Centralized + Fixed",
        "Centralized + Convergence",
        "Distributed + Fixed",
        "Distributed + Convergence"
    };

    StrategyResult results[4];

    printf("\n");
    printf("============================================================\n");
    printf("  PAGERANK EXECUTION STRATEGY COMPARISON  (Aleena - M2)\n");
    printf("============================================================\n");
    printf("  Graph: %d nodes\n", pg->num_nodes);
    printf("  Threads: %d  |  Max iters: %d  |  Threshold: %.0e\n",
           num_threads, max_iters, threshold);
    printf("============================================================\n\n");

    /* --- Run Strategy 1 --- */
    printf("[1/4] Centralized Aggregation + Fixed Iterations\n");
    results[0] = pagerank_centralized_fixed(pg, num_threads, max_iters);
    find_top_nodes(pg, TOP_K, top_ids[0], top_ranks[0]);
    printf("\n");

    /* --- Run Strategy 2 --- */
    printf("[2/4] Centralized Aggregation + Convergence Termination\n");
    results[1] = pagerank_centralized_convergence(pg, num_threads, threshold);
    find_top_nodes(pg, TOP_K, top_ids[1], top_ranks[1]);
    printf("\n");

    /* --- Run Strategy 3 --- */
    printf("[3/4] Distributed Reduction + Fixed Iterations\n");
    results[2] = pagerank_distributed_fixed(pg, num_threads, max_iters);
    find_top_nodes(pg, TOP_K, top_ids[2], top_ranks[2]);
    printf("\n");

    /* --- Run Strategy 4 --- */
    printf("[4/4] Distributed Reduction + Convergence Termination\n");
    results[3] = pagerank_distributed_convergence(pg, num_threads, threshold);
    find_top_nodes(pg, TOP_K, top_ids[3], top_ranks[3]);
    printf("\n");

    /* --- Find slowest for speedup calculation --- */
    double max_time = 0.0;
    for (int i = 0; i < 4; i++) {
        if (results[i].elapsed_ms > max_time) max_time = results[i].elapsed_ms;
    }

    /* --- Print summary table --- */
    printf("============================================================\n");
    printf("  COMPARISON RESULTS\n");
    printf("============================================================\n\n");

    printf("%-30s | %10s | %6s | %12s | %8s\n",
           "Strategy", "Time (ms)", "Iters", "Final Diff", "Speedup");
    printf("-------------------------------+------------+--------+--------------+----------\n");

    for (int i = 0; i < 4; i++) {
        double speedup = (results[i].elapsed_ms > 0.0)
                         ? max_time / results[i].elapsed_ms : 0.0;
        printf("%-30s | %10.2f | %6d | %12.4e | %7.2fx\n",
               names[i],
               results[i].elapsed_ms,
               results[i].iterations,
               results[i].final_diff,
               speedup);
    }

    /* --- Print top-K nodes per strategy --- */
    printf("\n%-30s | Top-%d Node IDs (by PageRank)\n", "Strategy", TOP_K);
    printf("-------------------------------+-------------------------------\n");
    for (int i = 0; i < 4; i++) {
        printf("%-30s |", names[i]);
        for (int k = 0; k < TOP_K && k < pg->num_nodes; k++) {
            printf(" %d(%.4e)", top_ids[i][k], top_ranks[i][k]);
        }
        printf("\n");
    }

    /* --- Correctness check: do all strategies agree on top-1? --- */
    printf("\n[Correctness] Top-1 node across strategies: ");
    int all_agree = 1;
    for (int i = 1; i < 4; i++) {
        if (top_ids[i][0] != top_ids[0][0]) { all_agree = 0; break; }
    }
    if (all_agree) {
        printf("PASS (all agree: node %d)\n", top_ids[0][0]);
    } else {
        printf("MISMATCH — ");
        for (int i = 0; i < 4; i++) printf("%d ", top_ids[i][0]);
        printf("\n");
    }


    if (output_file) {
        FILE *f = fopen(output_file, "w");
        if (f) {
            fprintf(f, "# PageRank Strategy Comparison Results (Aleena - Milestone 2)\n");
            fprintf(f, "# Graph: %d nodes\n", pg->num_nodes);
            fprintf(f, "# Threads: %d  |  Max iters: %d  |  Threshold: %.0e\n\n",
                    num_threads, max_iters, threshold);

            fprintf(f, "%-30s | %10s | %6s | %12s | %8s\n",
                    "Strategy", "Time (ms)", "Iters", "Final Diff", "Speedup");
            fprintf(f, "-------------------------------+------------+--------+--------------+----------\n");

            for (int i = 0; i < 4; i++) {
                double speedup = (results[i].elapsed_ms > 0.0)
                                 ? max_time / results[i].elapsed_ms : 0.0;
                fprintf(f, "%-30s | %10.2f | %6d | %12.4e | %7.2fx\n",
                        names[i],
                        results[i].elapsed_ms,
                        results[i].iterations,
                        results[i].final_diff,
                        speedup);
            }

            fprintf(f, "\nTop-%d nodes per strategy:\n", TOP_K);
            for (int i = 0; i < 4; i++) {
                fprintf(f, "  %s:", names[i]);
                for (int k = 0; k < TOP_K && k < pg->num_nodes; k++) {
                    fprintf(f, " node_%d(%.6e)", top_ids[i][k], top_ranks[i][k]);
                }
                fprintf(f, "\n");
            }

            fprintf(f, "\nCorrectness: top-1 %s (node %d)\n",
                    all_agree ? "PASS" : "MISMATCH", top_ids[0][0]);

            fclose(f);
            printf("\n[Output] Results saved to %s\n", output_file);
        }
    }

    printf("============================================================\n\n");
}
