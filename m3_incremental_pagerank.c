/*
 * m3_incremental_pagerank.c — Milestone 3: Incremental PageRank Strategies
 *
 * Implements and compares:
 *   1. Static Recomputation: Full PageRank from scratch on Phase 2 graph
 *   2. Incremental Recomputation: Leverage Phase 1 ranks as baseline
 *
 * Measures: convergence speed, accuracy, and computational cost trade-offs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
static double now_ms(void) { return (double)GetTickCount64(); }
#else
#include <sys/time.h>
static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

#include "m3_incremental_pagerank.h"
#include "parallel_pagerank.h"

static uint64_t estimate_graph_memory(IncrementalGraph *g) {
    uint64_t bytes = sizeof(IncrementalGraph);
    bytes += (uint64_t)g->num_nodes * sizeof(int);
    bytes += (uint64_t)g->num_nodes * sizeof(int*);
    bytes += (uint64_t)g->num_nodes * sizeof(double);
    for (int i = 0; i < g->num_nodes; i++) {
        if (g->adjacency[i]) {
            bytes += (uint64_t)g->out_degree[i] * sizeof(int);
        }
    }
    return bytes;
}

static uint64_t estimate_data_movement(int64_t active_count, uint64_t iterations) {
    return (uint64_t)active_count * sizeof(double) * 2ULL * iterations;
}

/* ============================================================
   Graph Loading and Management
   ============================================================ */

IncrementalGraph* incremental_load_graph(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[IncrementalPageRank] Error: Cannot open %s\n", filename);
        return NULL;
    }

    char line[4096];
    int num_nodes = 0;
    int max_node_id = -1;

    /* First pass: count nodes and find max node ID */
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        int node_id = atoi(line);
        if (node_id > max_node_id) max_node_id = node_id;
        num_nodes++;
    }

    num_nodes = max_node_id + 1;
    printf("[IncrementalPageRank] Loading graph: %d nodes from %s\n", num_nodes, filename);

    IncrementalGraph *g = (IncrementalGraph*)malloc(sizeof(IncrementalGraph));
    g->num_nodes = num_nodes;
    g->num_edges = 0;
    g->out_degree = (int*)calloc(num_nodes, sizeof(int));
    g->adjacency = (int**)malloc(num_nodes * sizeof(int*));
    g->ranks = (double*)malloc(num_nodes * sizeof(double));

    for (int i = 0; i < num_nodes; i++) {
        g->adjacency[i] = NULL;
    }

    /* Second pass: count outlinks */
    int *edge_count = (int*)calloc(num_nodes, sizeof(int));
    rewind(f);

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        int node_id = atoi(line);
        char *link_str = colon + 1;

        if (*link_str != '\n') {
            char *copy = strdup(link_str);
            char *saveptr;
            char *token = strtok_r(copy, ",", &saveptr);
            while (token) {
                edge_count[node_id]++;
                token = strtok_r(NULL, ",", &saveptr);
            }
            free(copy);
        }
    }

    /* Allocate adjacency lists */
    for (int i = 0; i < num_nodes; i++) {
        if (edge_count[i] > 0) {
            g->adjacency[i] = (int*)malloc(edge_count[i] * sizeof(int));
            g->out_degree[i] = edge_count[i];
        }
    }

    /* Third pass: populate adjacency lists */
    int *current_idx = (int*)calloc(num_nodes, sizeof(int));
    rewind(f);

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        int node_id = atoi(line);
        char *link_str = colon + 1;

        if (*link_str != '\n') {
            char *copy = strdup(link_str);
            char *saveptr;
            char *token = strtok_r(copy, ",", &saveptr);
            while (token) {
                int target = atoi(token);
                if (target >= 0 && target < num_nodes) {
                    g->adjacency[node_id][current_idx[node_id]++] = target;
                    g->num_edges++;
                }
                token = strtok_r(NULL, ",", &saveptr);
            }
            free(copy);
        }
    }

    free(edge_count);
    free(current_idx);
    fclose(f);

    /* Initialize ranks uniformly */
    double initial_rank = 1.0 / g->num_nodes;
    for (int i = 0; i < g->num_nodes; i++) {
        g->ranks[i] = initial_rank;
    }

    printf("[IncrementalPageRank] Loaded: %d nodes, %d edges\n", g->num_nodes, g->num_edges);
    return g;
}

void incremental_free_graph(IncrementalGraph *g) {
    if (!g) return;

    for (int i = 0; i < g->num_nodes; i++) {
        if (g->adjacency[i]) free(g->adjacency[i]);
    }

    free(g->adjacency);
    free(g->out_degree);
    free(g->ranks);
    free(g);
}

void incremental_free_stats(RecomputationStats *stats) {
    if (stats) free(stats);
}

void incremental_free_comparison(ComparisonResult *result) {
    if (!result) return;
    if (result->full) free(result->full);
    if (result->incremental) free(result->incremental);
    free(result);
}

/* ============================================================
   Parallel PageRank Computation (Full/Static) — Using parallel_pagerank.c
   ============================================================ */

RecomputationStats* incremental_pagerank_full(
    IncrementalGraph *phase2_graph,
    int num_threads) {

    printf("\n========================================================\n");
    printf("  STATIC PAGERANK: Full Recomputation\n");
    printf("========================================================\n");
    printf("  Nodes: %d, Threads: %d\n", phase2_graph->num_nodes, num_threads);
    printf("========================================================\n\n");

    double start_time = now_ms();

    /* Convert IncrementalGraph to ParallelGraph */
    ParallelGraph *pg = parallel_allocate_graph(phase2_graph->num_nodes);
    
    for (int i = 0; i < phase2_graph->num_nodes; i++) {
        pg->out_degree[i] = phase2_graph->out_degree[i];
        pg->current_rank[i] = 1.0 / phase2_graph->num_nodes;
        pg->next_rank[i] = 0.0;
        if (phase2_graph->adjacency[i]) {
            pg->adjacency[i] = (int*)malloc(phase2_graph->out_degree[i] * sizeof(int));
            memcpy(pg->adjacency[i], phase2_graph->adjacency[i], 
                   phase2_graph->out_degree[i] * sizeof(int));
        }
        pg->num_edges += phase2_graph->out_degree[i];
    }

    /* Run parallel PageRank */
    PageRankResult pr = parallel_pagerank_compute(pg, num_threads);

    double elapsed = now_ms() - start_time;

    /* Copy results back */
    for (int i = 0; i < phase2_graph->num_nodes; i++) {
        phase2_graph->ranks[i] = pg->current_rank[i];
    }

    printf("========================================================\n");
    printf("  Static PageRank Complete: %.0f ms\n\n", elapsed);

    parallel_free_graph(pg);

    RecomputationStats *stats = (RecomputationStats*)malloc(sizeof(RecomputationStats));
    stats->num_nodes_old = 0;
    stats->num_nodes_new = phase2_graph->num_nodes;
    stats->num_new_nodes = phase2_graph->num_nodes;
    stats->num_affected_nodes = phase2_graph->num_nodes;
    stats->iterations = (uint64_t)pr.iterations;
    stats->elapsed_ms = elapsed;
    stats->convergence_delta = pr.final_delta;
    stats->memory_bytes = estimate_graph_memory(phase2_graph) * 2ULL;
    stats->data_movement_bytes = estimate_data_movement(phase2_graph->num_nodes, stats->iterations);

    return stats;
}

/* ============================================================
   Incremental PageRank Computation — Using parallel_pagerank.c
   ============================================================ */

int* incremental_identify_affected_nodes(
    IncrementalGraph *phase1,
    IncrementalGraph *phase2,
    int *affected_count) {

    int *affected = (int*)malloc(phase2->num_nodes * sizeof(int));
    int count = 0;

    /* Mark all new nodes */
    for (int i = phase1->num_nodes; i < phase2->num_nodes; i++) {
        affected[count++] = i;
    }

    /* Mark existing nodes with changed connectivity */
    for (int i = 0; i < phase1->num_nodes && i < phase2->num_nodes; i++) {
        bool changed = false;

        /* Check if out-degree changed */
        if (phase1->out_degree[i] != phase2->out_degree[i]) {
            changed = true;
        } else {
            /* Check if outlinks changed */
            for (int j = 0; j < phase1->out_degree[i]; j++) {
                if (phase1->adjacency[i][j] != phase2->adjacency[i][j]) {
                    changed = true;
                    break;
                }
            }
        }

        if (changed) {
            affected[count++] = i;
        }
    }

    *affected_count = count;
    return affected;
}

typedef struct {
    IncrementalGraph *graph;
    int **reverse_adjacency;
    int *reverse_degree;
    int *active_nodes;
    int start_index;
    int end_index;
    double *current_rank;
    double *next_rank;
    double local_delta;
} IncrementalActiveWorkerArgs;

static int **incremental_build_reverse_adjacency(
    IncrementalGraph *graph,
    int **out_reverse_degree) {

    int num_nodes = graph->num_nodes;
    int *reverse_degree = (int*)calloc(num_nodes, sizeof(int));
    if (!reverse_degree) return NULL;

    for (int source = 0; source < num_nodes; source++) {
        for (int i = 0; i < graph->out_degree[source]; i++) {
            int target = graph->adjacency[source][i];
            if (target >= 0 && target < num_nodes) {
                reverse_degree[target]++;
            }
        }
    }

    int **reverse_adjacency = (int**)malloc(num_nodes * sizeof(int*));
    if (!reverse_adjacency) {
        free(reverse_degree);
        return NULL;
    }

    for (int i = 0; i < num_nodes; i++) {
        reverse_adjacency[i] = NULL;
        if (reverse_degree[i] > 0) {
            reverse_adjacency[i] = (int*)malloc(reverse_degree[i] * sizeof(int));
            if (!reverse_adjacency[i]) {
                for (int j = 0; j < i; j++) {
                    free(reverse_adjacency[j]);
                }
                free(reverse_adjacency);
                free(reverse_degree);
                return NULL;
            }
        }
    }

    int *fill_index = (int*)calloc(num_nodes, sizeof(int));
    if (!fill_index) {
        for (int i = 0; i < num_nodes; i++) {
            free(reverse_adjacency[i]);
        }
        free(reverse_adjacency);
        free(reverse_degree);
        return NULL;
    }

    for (int source = 0; source < num_nodes; source++) {
        for (int i = 0; i < graph->out_degree[source]; i++) {
            int target = graph->adjacency[source][i];
            if (target >= 0 && target < num_nodes && reverse_adjacency[target]) {
                reverse_adjacency[target][fill_index[target]++] = source;
            }
        }
    }

    free(fill_index);
    *out_reverse_degree = reverse_degree;
    return reverse_adjacency;
}

static void incremental_free_reverse_adjacency(int **reverse_adjacency, int num_nodes) {
    if (!reverse_adjacency) return;
    for (int i = 0; i < num_nodes; i++) {
        free(reverse_adjacency[i]);
    }
    free(reverse_adjacency);
}

static int* incremental_expand_active_region(
    IncrementalGraph *graph,
    const int *seed_nodes,
    int seed_count,
    int hop_limit,
    int **out_reverse_degree,
    int ***out_reverse_adjacency,
    int *out_active_count) {

    int num_nodes = graph->num_nodes;
    int *reverse_degree = NULL;
    int **reverse_adjacency = incremental_build_reverse_adjacency(graph, &reverse_degree);
    if (!reverse_adjacency) return NULL;

    unsigned char *active_mask = (unsigned char*)calloc(num_nodes, sizeof(unsigned char));
    int *distance = (int*)malloc(num_nodes * sizeof(int));
    int *queue = (int*)malloc(num_nodes * sizeof(int));
    if (!active_mask || !distance || !queue) {
        free(active_mask);
        free(distance);
        free(queue);
        incremental_free_reverse_adjacency(reverse_adjacency, num_nodes);
        free(reverse_degree);
        return NULL;
    }

    for (int i = 0; i < num_nodes; i++) {
        distance[i] = -1;
    }

    int head = 0;
    int tail = 0;
    for (int i = 0; i < seed_count; i++) {
        int node = seed_nodes[i];
        if (node < 0 || node >= num_nodes || active_mask[node]) continue;
        active_mask[node] = 1;
        distance[node] = 0;
        queue[tail++] = node;
    }

    while (head < tail) {
        int node = queue[head++];
        int depth = distance[node];
        if (depth >= hop_limit) continue;

        for (int i = 0; i < graph->out_degree[node]; i++) {
            int neighbor = graph->adjacency[node][i];
            if (neighbor >= 0 && neighbor < num_nodes && !active_mask[neighbor]) {
                active_mask[neighbor] = 1;
                distance[neighbor] = depth + 1;
                queue[tail++] = neighbor;
            }
        }

        for (int i = 0; i < reverse_degree[node]; i++) {
            int neighbor = reverse_adjacency[node][i];
            if (neighbor >= 0 && neighbor < num_nodes && !active_mask[neighbor]) {
                active_mask[neighbor] = 1;
                distance[neighbor] = depth + 1;
                queue[tail++] = neighbor;
            }
        }
    }

    int active_count = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (active_mask[i]) active_count++;
    }

    int *active_nodes = (int*)malloc(active_count * sizeof(int));
    if (!active_nodes) {
        free(active_mask);
        free(distance);
        free(queue);
        incremental_free_reverse_adjacency(reverse_adjacency, num_nodes);
        free(reverse_degree);
        return NULL;
    }

    int index = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (active_mask[i]) {
            active_nodes[index++] = i;
        }
    }

    free(active_mask);
    free(distance);
    free(queue);

    *out_reverse_degree = reverse_degree;
    *out_reverse_adjacency = reverse_adjacency;
    *out_active_count = active_count;
    return active_nodes;
}

static void* incremental_active_iteration_worker(void *args) {
    IncrementalActiveWorkerArgs *work = (IncrementalActiveWorkerArgs*)args;
    IncrementalGraph *graph = work->graph;
    double base_rank = (1.0 - DAMPING_FACTOR) / graph->num_nodes;
    double local_delta = 0.0;

    for (int idx = work->start_index; idx < work->end_index; idx++) {
        int node = work->active_nodes[idx];
        double rank = base_rank;

        for (int i = 0; i < work->reverse_degree[node]; i++) {
            int source = work->reverse_adjacency[node][i];
            if (graph->out_degree[source] > 0) {
                rank += DAMPING_FACTOR * (work->current_rank[source] / graph->out_degree[source]);
            }
        }

        local_delta += fabs(rank - work->current_rank[node]);
        work->next_rank[node] = rank;
    }

    work->local_delta = local_delta;
    return NULL;
}

RecomputationStats* incremental_pagerank_incremental(
    IncrementalGraph *phase1_graph,
    IncrementalGraph *phase2_graph,
    int num_threads) {

    printf("\n========================================================\n");
    printf("  INCREMENTAL PAGERANK: Warm-Start with Phase 1 Baseline\n");
    printf("========================================================\n");
    printf("  Phase 2 Union Graph: %d total nodes\n", phase2_graph->num_nodes);
    printf("  Threads: %d\n", num_threads);
    printf("========================================================\n\n");

    /* Read phase2_metadata.txt to find where Phase 1 nodes end */
    int phase1_node_count = phase1_graph->num_nodes;
    int phase2_new_nodes = phase2_graph->num_nodes - phase1_node_count;
    
    FILE *meta = fopen("phase2_metadata.txt", "r");
    if (meta) {
        char line[256];
        while (fgets(line, sizeof(line), meta)) {
            if (sscanf(line, "phase1_node_count: %d", &phase1_node_count) == 1) {
                phase2_new_nodes = phase2_graph->num_nodes - phase1_node_count;
                break;
            }
        }
        fclose(meta);
    }
    
    printf("  [Incremental] Node ranges detected:\n");
    printf("    - Phase 1 nodes: 0 to %d (count: %d)\n", phase1_node_count - 1, phase1_node_count);
    printf("    - Phase 2 new nodes: %d to %d (count: %d)\n", 
           phase1_node_count, phase2_graph->num_nodes - 1, phase2_new_nodes);

    /* Load Phase 1 ranks for warm-start initialization */
    printf("  [Incremental] Loading Phase 1 ranks for warm-start...\n");
    FILE *phase1_ranks_file = fopen("phase1_ranks.txt", "r");
    if (!phase1_ranks_file) {
        fprintf(stderr, "[IncrementalPageRank] Error: Cannot open phase1_ranks.txt\n");
        return NULL;
    }

    int nodes_initialized_from_file = 0;
    char line[256];
    int node_id;
    double rank;
    while (fgets(line, sizeof(line), phase1_ranks_file) && nodes_initialized_from_file < phase1_node_count) {
        if (sscanf(line, "%d %lf", &node_id, &rank) == 2) {
            if (node_id >= 0 && node_id < phase1_node_count) {
                phase2_graph->ranks[node_id] = rank;
                nodes_initialized_from_file++;
            }
        }
    }
    fclose(phase1_ranks_file);

    printf("  [Incremental] Initialized %d Phase 1 nodes with saved ranks\n",
           nodes_initialized_from_file);

    /* Initialize Phase 2 new nodes with teleportation probability and
       scale Phase 1 ranks so total probability mass remains 1.0 */
    double teleport_rank = (1.0 - DAMPING_FACTOR) / (double)phase2_graph->num_nodes;
    int new_count = phase2_graph->num_nodes - phase1_node_count;
    for (int i = phase1_node_count; i < phase2_graph->num_nodes; i++) {
        phase2_graph->ranks[i] = teleport_rank;
    }

    /* Compute Phase 1 sum and scale to preserve total mass = 1.0 */
    double sum_phase1 = 0.0;
    for (int i = 0; i < phase1_node_count; i++) sum_phase1 += phase2_graph->ranks[i];
    double sum_new = teleport_rank * (double)new_count;

    if (sum_phase1 <= 0.0) {
        /* Fallback: uniform distribution over all nodes */
        double uniform = 1.0 / (double)phase2_graph->num_nodes;
        for (int i = 0; i < phase2_graph->num_nodes; i++) phase2_graph->ranks[i] = uniform;
        printf("  [Incremental] Warning: Phase1 ranks sum to zero; normalized uniformly\n");
    } else {
        double scale = (1.0 - sum_new) / sum_phase1;
        for (int i = 0; i < phase1_node_count; i++) phase2_graph->ranks[i] *= scale;
    }

    printf("  [Incremental] Initialized %d Phase 2 new nodes with teleport_rank=%.6e and scaled Phase 1 ranks (sum_new=%.6e)\n\n",
           new_count, teleport_rank, sum_new);

    /* Identify affected nodes and expand the frontier to nearby hops. */
    int seed_count = 0;
    int *seed_nodes = incremental_identify_affected_nodes(phase1_graph, phase2_graph, &seed_count);
    if (!seed_nodes) {
        fprintf(stderr, "[IncrementalPageRank] Error: failed to identify affected nodes\n");
        return NULL;
    }

    int *reverse_degree = NULL;
    int **reverse_adjacency = NULL;
    int active_count = 0;
    const int hop_limit = 3;
    int *active_nodes = incremental_expand_active_region(
        phase2_graph,
        seed_nodes,
        seed_count,
        hop_limit,
        &reverse_degree,
        &reverse_adjacency,
        &active_count);

    free(seed_nodes);

    if (!active_nodes) {
        fprintf(stderr, "[IncrementalPageRank] Error: failed to expand affected region\n");
        return NULL;
    }

    printf("  [Incremental] Seed affected nodes: %d\n", seed_count);
    printf("  [Incremental] Active region expanded to %d nodes within %d hops\n",
           active_count, hop_limit);
    printf("  [Incremental] Only active nodes are updated; others are frozen\n");

    double start_time = now_ms();

    double *current_rank = phase2_graph->ranks;
    double *next_rank = (double*)malloc(phase2_graph->num_nodes * sizeof(double));
    if (!next_rank) {
        incremental_free_reverse_adjacency(reverse_adjacency, phase2_graph->num_nodes);
        free(reverse_degree);
        free(active_nodes);
        fprintf(stderr, "[IncrementalPageRank] Error: out of memory allocating next_rank\n");
        return NULL;
    }

    memcpy(next_rank, current_rank, phase2_graph->num_nodes * sizeof(double));

    int iter = 0;
    double final_delta = 0.0;

    while (iter < MAX_ITERATIONS) {
        memcpy(next_rank, current_rank, phase2_graph->num_nodes * sizeof(double));

        pthread_t *threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
        IncrementalActiveWorkerArgs **worker_args = (IncrementalActiveWorkerArgs**)malloc(num_threads * sizeof(IncrementalActiveWorkerArgs*));
        if (!threads || !worker_args) {
            free(threads);
            free(worker_args);
            free(next_rank);
            incremental_free_reverse_adjacency(reverse_adjacency, phase2_graph->num_nodes);
            free(reverse_degree);
            free(active_nodes);
            fprintf(stderr, "[IncrementalPageRank] Error: out of memory creating workers\n");
            return NULL;
        }

        int nodes_per_thread = (active_count + num_threads - 1) / num_threads;
        if (nodes_per_thread < 1) nodes_per_thread = 1;

        for (int t = 0; t < num_threads; t++) {
            int start = t * nodes_per_thread;
            int end = start + nodes_per_thread;
            if (start > active_count) start = active_count;
            if (end > active_count) end = active_count;

            worker_args[t] = (IncrementalActiveWorkerArgs*)malloc(sizeof(IncrementalActiveWorkerArgs));
            worker_args[t]->graph = phase2_graph;
            worker_args[t]->reverse_adjacency = reverse_adjacency;
            worker_args[t]->reverse_degree = reverse_degree;
            worker_args[t]->active_nodes = active_nodes;
            worker_args[t]->start_index = start;
            worker_args[t]->end_index = end;
            worker_args[t]->current_rank = current_rank;
            worker_args[t]->next_rank = next_rank;
            worker_args[t]->local_delta = 0.0;

            pthread_create(&threads[t], NULL, incremental_active_iteration_worker, worker_args[t]);
        }

        final_delta = 0.0;
        for (int t = 0; t < num_threads; t++) {
            pthread_join(threads[t], NULL);
            final_delta += worker_args[t]->local_delta;
            free(worker_args[t]);
        }

        free(worker_args);
        free(threads);

        double *swap = current_rank;
        current_rank = next_rank;
        next_rank = swap;

        printf("  Iteration %3d: delta=%e\n", iter, final_delta);
        if (final_delta < CONVERGENCE_THRESHOLD) {
            printf("\n  Converged at iteration %d\n", iter);
            break;
        }

        iter++;
    }

    if (current_rank != phase2_graph->ranks) {
        memcpy(phase2_graph->ranks, current_rank, phase2_graph->num_nodes * sizeof(double));
    }

    free(next_rank);
    incremental_free_reverse_adjacency(reverse_adjacency, phase2_graph->num_nodes);
    free(reverse_degree);
    free(active_nodes);

    uint64_t reverse_edges = 0;
    for (int i = 0; i < phase2_graph->num_nodes; i++) {
        reverse_edges += reverse_degree[i];
    }

    uint64_t memory_bytes = estimate_graph_memory(phase2_graph);
    memory_bytes += (uint64_t)phase2_graph->num_nodes * sizeof(double);      /* next_rank */
    memory_bytes += (uint64_t)phase2_graph->num_nodes * sizeof(int);         /* reverse_degree */
    memory_bytes += (uint64_t)phase2_graph->num_nodes * sizeof(int*);        /* reverse_adjacency pointers */
    memory_bytes += reverse_edges * sizeof(int);                             /* reverse adjacency entries */
    memory_bytes += (uint64_t)phase2_graph->num_nodes * sizeof(unsigned char); /* active_mask */
    memory_bytes += (uint64_t)phase2_graph->num_nodes * sizeof(int) * 2;     /* distance + queue */
    memory_bytes += (uint64_t)active_count * sizeof(int);                    /* active_nodes */
    memory_bytes += (uint64_t)seed_count * sizeof(int);                      /* seed_nodes */

    double elapsed = now_ms() - start_time;

    printf("========================================================\n");
    printf("  Incremental PageRank Complete: %.0f ms\n\n", elapsed);

    RecomputationStats *stats = (RecomputationStats*)malloc(sizeof(RecomputationStats));
    stats->num_nodes_old = phase1_graph->num_nodes;
    stats->num_nodes_new = phase2_graph->num_nodes;
    stats->num_new_nodes = phase2_graph->num_nodes - phase1_graph->num_nodes;
    stats->num_affected_nodes = active_count;
    stats->iterations = (uint64_t)(iter + 1);
    stats->elapsed_ms = elapsed;
    stats->convergence_delta = final_delta;
    stats->memory_bytes = memory_bytes;
    stats->data_movement_bytes = estimate_data_movement(active_count, stats->iterations);

    return stats;
}

/* ============================================================
   Strategy Comparison
   ============================================================ */

ComparisonResult* incremental_compare_strategies(
    IncrementalGraph *phase1,
    IncrementalGraph *phase2,
    int num_threads) {

    printf("\n");
    printf("========================================================\n");
    printf("  PAGERANK RECOMPUTATION STRATEGY COMPARISON\n");
    printf("========================================================\n");

    /* Make copies for independent computation */
    IncrementalGraph *phase2_copy1 = (IncrementalGraph*)malloc(sizeof(IncrementalGraph));
    IncrementalGraph *phase2_copy2 = (IncrementalGraph*)malloc(sizeof(IncrementalGraph));

    /* Copy Phase 2 structure */
#define COPY_GRAPH(src, dst) do { \
    (dst)->num_nodes = (src)->num_nodes; \
    (dst)->num_edges = (src)->num_edges; \
    (dst)->out_degree = (int*)malloc((src)->num_nodes * sizeof(int)); \
    memcpy((dst)->out_degree, (src)->out_degree, (src)->num_nodes * sizeof(int)); \
    (dst)->adjacency = (int**)malloc((src)->num_nodes * sizeof(int*)); \
    for (int i = 0; i < (src)->num_nodes; i++) { \
        if ((src)->adjacency[i]) { \
            (dst)->adjacency[i] = (int*)malloc((src)->out_degree[i] * sizeof(int)); \
            memcpy((dst)->adjacency[i], (src)->adjacency[i], (src)->out_degree[i] * sizeof(int)); \
        } else { \
            (dst)->adjacency[i] = NULL; \
        } \
    } \
    (dst)->ranks = (double*)malloc((src)->num_nodes * sizeof(double)); \
    memcpy((dst)->ranks, (src)->ranks, (src)->num_nodes * sizeof(double)); \
} while(0)

    COPY_GRAPH(phase2, phase2_copy1);
    COPY_GRAPH(phase2, phase2_copy2);

    /* Strategy 1: Full Recomputation */
    RecomputationStats *full_stats = incremental_pagerank_full(phase2_copy1, num_threads);
    full_stats->num_nodes_old = phase1->num_nodes;
    full_stats->num_nodes_new = phase2->num_nodes;
    full_stats->num_new_nodes = phase2->num_nodes - phase1->num_nodes;
    full_stats->num_affected_nodes = phase2->num_nodes;

    /* Strategy 2: Incremental Recomputation */
    RecomputationStats *incr_stats = incremental_pagerank_incremental(phase1, phase2_copy2, num_threads);

    /* Compute comparison metrics */
    double speedup = full_stats->elapsed_ms / (incr_stats->elapsed_ms + 1e-6);
    double avg_rank_diff = 0.0;
    double max_rank_diff = 0.0;
    double l1_rank_diff = 0.0;
    double l2_rank_diff = 0.0;

    for (int i = 0; i < phase2->num_nodes; i++) {
        double diff = fabs(phase2_copy1->ranks[i] - phase2_copy2->ranks[i]);
        avg_rank_diff += diff;
        l1_rank_diff += diff;
        l2_rank_diff += diff * diff;
        if (diff > max_rank_diff) max_rank_diff = diff;
    }
    avg_rank_diff /= phase2->num_nodes;
    l2_rank_diff = sqrt(l2_rank_diff);

    ComparisonResult *result = (ComparisonResult*)malloc(sizeof(ComparisonResult));
    result->l1_rank_diff = l1_rank_diff;
    result->l2_rank_diff = l2_rank_diff;

    /* Print comparison */
    printf("\n");
    printf("+--------------------------------------------------------------------------+\n");
    printf("| COMPARISON RESULTS                                                       |\n");
    printf("+---------------------------+----------------+-----------------------------+\n");
    printf("| Metric                    | Static         | Incremental                 |\n");
    printf("+---------------------------+----------------+-----------------------------+\n");
    printf("| Execution Time (ms)       | %12.1f | %12.1f              |\n",
           full_stats->elapsed_ms, incr_stats->elapsed_ms);
    printf("| Iterations to Converge    | %12" PRIu64 " | %12" PRIu64 "              |\n",
           full_stats->iterations, incr_stats->iterations);
    printf("| Final Convergence Delta   | %12e | %12e              |\n",
           full_stats->convergence_delta, incr_stats->convergence_delta);
    printf("| Memory Footprint (bytes)  | %12" PRIu64 " | %12" PRIu64 "              |\n",
           full_stats->memory_bytes, incr_stats->memory_bytes);
    printf("| Data Movement (bytes)     | %12" PRIu64 " | %12" PRIu64 "              |\n",
           full_stats->data_movement_bytes, incr_stats->data_movement_bytes);
    printf("| Nodes Affected            | %12d | %12d              |\n",
           full_stats->num_affected_nodes, incr_stats->num_affected_nodes);
    printf("+---------------------------+----------------+-----------------------------+\n");
    printf("| Speedup (Incremental)     | %.2fx faster                                     |\n", speedup);
    printf("| Avg Rank Difference       | %.6e                                 |\n", avg_rank_diff);
    printf("| Max Rank Difference       | %.6e                                 |\n", max_rank_diff);
    printf("| L1 Rank Difference        | %.6e                                 |\n", result->l1_rank_diff);
    printf("| L2 Rank Difference        | %.6e                                 |\n", result->l2_rank_diff);
    printf("+--------------------------------------------------------------------------+\n");

    /* Save incremental ranks */
    printf("\n[Comparison] Saving incremental PageRank results to phase2_ranks_incremental.txt\n");
    incremental_save_ranks(phase2_copy2, "phase2_ranks_incremental.txt");

    /* Save full ranks */
    printf("[Comparison] Saving full PageRank results to phase2_ranks_full.txt\n");
    incremental_save_ranks(phase2_copy1, "phase2_ranks_full.txt");

    /* Cleanup temporary copies */
    incremental_free_graph(phase2_copy1);
    incremental_free_graph(phase2_copy2);

    result->full = full_stats;
    result->incremental = incr_stats;
    result->speedup = speedup;
    result->avg_rank_diff = avg_rank_diff;
    result->max_rank_diff = max_rank_diff;

    return result;
}

/* ============================================================
   File-Based Strategy Comparison (Memory Efficient)
   Loads graphs on-demand, avoids keeping large graphs in RAM
   ============================================================ */

ComparisonResult* incremental_compare_strategies_files(
    const char *phase1_graph_file,
    const char *phase2_graph_file,
    int num_threads) {

    printf("\n");
    printf("+--------------------------------------------------------------------------+\n");
    printf("| LOADING GRAPHS FROM FILES (Memory-Efficient Mode)                       |\n");
    printf("+--------------------------------------------------------------------------+\n");

    /* Load Phase 1 graph */
    printf("[FileComparison] Loading Phase 1 from %s...\n", phase1_graph_file);
    IncrementalGraph *phase1 = incremental_load_graph(phase1_graph_file);
    if (!phase1) {
        fprintf(stderr, "ERROR: Failed to load %s\n", phase1_graph_file);
        return NULL;
    }
    printf("[FileComparison] Phase 1: %d nodes, %d edges\n", phase1->num_nodes, phase1->num_edges);

    /* Load Phase 2 graph */
    printf("[FileComparison] Loading Phase 2 from %s...\n", phase2_graph_file);
    IncrementalGraph *phase2 = incremental_load_graph(phase2_graph_file);
    if (!phase2) {
        fprintf(stderr, "ERROR: Failed to load %s\n", phase2_graph_file);
        incremental_free_graph(phase1);
        return NULL;
    }
    printf("[FileComparison] Phase 2: %d nodes, %d edges\n", phase2->num_nodes, phase2->num_edges);

    /* Call the existing comparison function */
    ComparisonResult *result = incremental_compare_strategies(phase1, phase2, num_threads);

    /* Free graphs after comparison */
    printf("[FileComparison] Freeing Phase 1 and Phase 2 graphs from memory...\n");
    incremental_free_graph(phase1);
    incremental_free_graph(phase2);
    printf("[FileComparison] Memory freed\n");

    return result;
}

/* ============================================================
   Output Functions
   ============================================================ */

void incremental_save_ranks(IncrementalGraph *g, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Error opening output file");
        return;
    }

    for (int i = 0; i < g->num_nodes; i++) {
        fprintf(f, "%d %.15e\n", i, g->ranks[i]);
    }

    fclose(f);
    printf("[IncrementalPageRank] Saved ranks to %s\n", filename);
}

void incremental_print_ranks(IncrementalGraph *g, int top_k) {
    printf("\nTop %d nodes by PageRank:\n", top_k);

    typedef struct {
        int node_id;
        double rank;
    } NodeRank;

    NodeRank *nodes = (NodeRank*)malloc(g->num_nodes * sizeof(NodeRank));
    for (int i = 0; i < g->num_nodes; i++) {
        nodes[i].node_id = i;
        nodes[i].rank = g->ranks[i];
    }

    /* Selection sort for top k */
    int k = (g->num_nodes < top_k) ? g->num_nodes : top_k;
    for (int i = 0; i < k; i++) {
        int max_idx = i;
        for (int j = i + 1; j < g->num_nodes; j++) {
            if (nodes[j].rank > nodes[max_idx].rank) {
                max_idx = j;
            }
        }
        NodeRank temp = nodes[i];
        nodes[i] = nodes[max_idx];
        nodes[max_idx] = temp;
    }

    for (int i = 0; i < k; i++) {
        printf("  %2d. Node %5d: %.15e\n", i + 1, nodes[i].node_id, nodes[i].rank);
    }

    free(nodes);
    printf("\n");
}
