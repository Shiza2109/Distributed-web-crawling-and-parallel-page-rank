/* pagerank.c - Corrected parallel PageRank */
#include "pagerank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <float.h>

/* ============================================================
   Graph Loader
   ============================================================ */

PageRankGraph* pagerank_load_graph(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[PageRank] Error: Cannot open %s\n", filename);
        return NULL;
    }
    
    PageRankGraph *pg = malloc(sizeof(PageRankGraph));
    if (!pg) {
        fclose(f);
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
    
    pg->num_nodes = max_node_id + 1;
    
    /* Allocate adjacency lists */
    pg->outlink_counts = calloc(pg->num_nodes, sizeof(int));
    pg->outlinks = malloc(pg->num_nodes * sizeof(int*));
    pg->ranks = malloc(pg->num_nodes * sizeof(double));
    pg->new_ranks = malloc(pg->num_nodes * sizeof(double));
    
    if (!pg->outlink_counts || !pg->outlinks || !pg->ranks || !pg->new_ranks) {
        fprintf(stderr, "[PageRank] Memory allocation failed\n");
        pagerank_free(pg);
        fclose(f);
        return NULL;
    }
    
    for (int i = 0; i < pg->num_nodes; i++) {
        pg->outlinks[i] = NULL;
    }
    
    /* Second pass: read edges */
    rewind(f);
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        char *outlink_str = colon + 1;
        
        /* Count outlinks */
        int outlink_count = 0;
        if (outlink_str[0] != '\n' && outlink_str[0] != '\0') {
            outlink_count = 1;
            for (int k = 0; outlink_str[k] != '\0' && outlink_str[k] != '\n'; k++) {
                if (outlink_str[k] == ',') {
                    outlink_count++;
                }
            }
        }
        
        pg->outlink_counts[node_id] = outlink_count;
        
        if (outlink_count > 0) {
            pg->outlinks[node_id] = malloc(outlink_count * sizeof(int));
            if (!pg->outlinks[node_id]) {
                fprintf(stderr, "[PageRank] Memory allocation failed for node %d\n", node_id);
                pagerank_free(pg);
                fclose(f);
                return NULL;
            }
            
            /* Reset to beginning of outlink string */
            outlink_str = colon + 1;
            int idx = 0;
            char *saveptr;
            char *token = strtok_r(outlink_str, ",", &saveptr);
            while (token && idx < outlink_count) {
                pg->outlinks[node_id][idx++] = atoi(token);
                token = strtok_r(NULL, ",", &saveptr);
            }
        } else {
            pg->outlinks[node_id] = NULL;
        }
    }
    
    fclose(f);
    
    printf("[PageRank] Loaded graph: %d nodes\n", pg->num_nodes);
    int total_edges = 0;
    for (int i = 0; i < pg->num_nodes; i++) {
        total_edges += pg->outlink_counts[i];
    }
    printf("[PageRank] Total edges: %d\n", total_edges);
    
    return pg;
}

/* ============================================================
   Sequential PageRank
   ============================================================ */

void pagerank_compute_sequential(PageRankGraph *pg) {
    if (!pg) return;
    
    int N = pg->num_nodes;
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;
    
    /* Initialize ranks to 1/N */
    double init_rank = 1.0 / N;
    for (int i = 0; i < N; i++) {
        pg->ranks[i] = init_rank;
        pg->new_ranks[i] = 0.0;
    }
    
    printf("[PageRank] Sequential: Starting iterations (N=%d, damping=%.2f, threshold=%.0e)\n",
           N, damping, CONVERGENCE_THRESHOLD);
    
    for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
        /* Reset new_ranks to teleportation component */
        for (int i = 0; i < N; i++) {
            pg->new_ranks[i] = teleport;
        }
        
        /* Distribute rank from each node to its outlinks */
        for (int i = 0; i < N; i++) {
            double rank_contrib = damping * pg->ranks[i];
            
            if (pg->outlink_counts[i] > 0) {
                double share = rank_contrib / pg->outlink_counts[i];
                for (int j = 0; j < pg->outlink_counts[i]; j++) {
                    int target = pg->outlinks[i][j];
                    pg->new_ranks[target] += share;
                }
            } else {
                /* Dangling page: distribute to all nodes equally */
                double share = rank_contrib / N;
                for (int j = 0; j < N; j++) {
                    pg->new_ranks[j] += share;
                }
            }
        }
        
        /* Check convergence and update ranks */
        double diff = 0.0;
        for (int i = 0; i < N; i++) {
            diff += fabs(pg->new_ranks[i] - pg->ranks[i]);
            pg->ranks[i] = pg->new_ranks[i];
        }
        
        if (iter % 10 == 0 || iter == MAX_ITERATIONS - 1) {
            printf("[PageRank] Iteration %d: L1 diff = %.6e\n", iter + 1, diff);
        }
        
        if (diff < CONVERGENCE_THRESHOLD) {
            printf("[PageRank] Converged after %d iterations\n", iter + 1);
            break;
        }
    }
}

/* ============================================================
   Parallel PageRank - Thread Arguments (FIXED)
   ============================================================ */

typedef struct {
    PageRankGraph *pg;
    int start_node;
    int end_node;
    double teleport;
    double damping;
    int N;
    pthread_mutex_t *node_mutexes;  /* Proper pointer to mutex array */
} PRThreadArgs;

/* Phase 1: Reset new_ranks for assigned nodes */
static void* reset_ranks_worker(void *arg) {
    PRThreadArgs *args = (PRThreadArgs*)arg;
    PageRankGraph *pg = args->pg;
    double teleport = args->teleport;
    
    for (int i = args->start_node; i < args->end_node; i++) {
        pg->new_ranks[i] = teleport;
    }
    
    return NULL;
}

/* Phase 2: Distribute ranks from assigned nodes */
static void* distribute_ranks_worker(void *arg) {
    PRThreadArgs *args = (PRThreadArgs*)arg;
    PageRankGraph *pg = args->pg;
    int N = args->N;
    double damping = args->damping;
    pthread_mutex_t *node_mutexes = args->node_mutexes;
    
    for (int i = args->start_node; i < args->end_node; i++) {
        double rank_contrib = damping * pg->ranks[i];
        
        if (pg->outlink_counts[i] > 0) {
            double share = rank_contrib / pg->outlink_counts[i];
            for (int j = 0; j < pg->outlink_counts[i]; j++) {
                int target = pg->outlinks[i][j];
                
                /* Lock mutex for target node */
                pthread_mutex_lock(&node_mutexes[target]);
                pg->new_ranks[target] += share;
                pthread_mutex_unlock(&node_mutexes[target]);
            }
        } else {
            /* Dangling page: distribute to all nodes */
            double share = rank_contrib / N;
            for (int j = 0; j < N; j++) {
                pthread_mutex_lock(&node_mutexes[j]);
                pg->new_ranks[j] += share;
                pthread_mutex_unlock(&node_mutexes[j]);
            }
        }
    }
    
    return NULL;
}

void pagerank_compute_parallel(PageRankGraph *pg, int num_threads) {
    if (!pg || num_threads <= 0) return;
    
    int N = pg->num_nodes;
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;
    
    /* Initialize ranks */
    double init_rank = 1.0 / N;
    for (int i = 0; i < N; i++) {
        pg->ranks[i] = init_rank;
        pg->new_ranks[i] = 0.0;
    }
    
    printf("[PageRank] Parallel: Starting with %d threads (N=%d, damping=%.2f)\n",
           num_threads, N, damping);
    
    /* Create mutexes for each node (for thread-safe rank updates) */
    pthread_mutex_t *node_mutexes = malloc(N * sizeof(pthread_mutex_t));
    for (int i = 0; i < N; i++) {
        pthread_mutex_init(&node_mutexes[i], NULL);
    }
    
    /* Calculate node distribution among threads */
    int nodes_per_thread = (N + num_threads - 1) / num_threads;
    
    for (int iter = 0; iter < MAX_ITERATIONS; iter++) {
        pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
        PRThreadArgs *args = malloc(num_threads * sizeof(PRThreadArgs));
        int active_threads = 0;
        
        /* Phase 1: Reset new_ranks to teleportation component */
        for (int t = 0; t < num_threads; t++) {
            int start = t * nodes_per_thread;
            int end = (t + 1) * nodes_per_thread;
            if (end > N) end = N;
            
            if (start >= N) break;
            
            args[t].pg = pg;
            args[t].start_node = start;
            args[t].end_node = end;
            args[t].teleport = teleport;
            args[t].damping = damping;
            args[t].N = N;
            args[t].node_mutexes = node_mutexes;
            
            pthread_create(&threads[t], NULL, reset_ranks_worker, &args[t]);
            active_threads++;
        }
        
        for (int t = 0; t < active_threads; t++) {
            pthread_join(threads[t], NULL);
        }
        
        /* Phase 2: Distribute ranks from each node */
        active_threads = 0;
        for (int t = 0; t < num_threads; t++) {
            int start = t * nodes_per_thread;
            int end = (t + 1) * nodes_per_thread;
            if (end > N) end = N;
            
            if (start >= N) break;
            
            args[t].pg = pg;
            args[t].start_node = start;
            args[t].end_node = end;
            args[t].teleport = teleport;
            args[t].damping = damping;
            args[t].N = N;
            args[t].node_mutexes = node_mutexes;
            
            pthread_create(&threads[t], NULL, distribute_ranks_worker, &args[t]);
            active_threads++;
        }
        
        for (int t = 0; t < active_threads; t++) {
            pthread_join(threads[t], NULL);
        }
        
        /* Phase 3: Check convergence and update ranks */
        double diff = 0.0;
        for (int i = 0; i < N; i++) {
            diff += fabs(pg->new_ranks[i] - pg->ranks[i]);
            pg->ranks[i] = pg->new_ranks[i];
        }
        
        if (iter % 10 == 0 || iter == MAX_ITERATIONS - 1) {
            printf("[PageRank] Iteration %d: L1 diff = %.6e\n", iter + 1, diff);
        }
        
        if (diff < CONVERGENCE_THRESHOLD) {
            printf("[PageRank] Converged after %d iterations\n", iter + 1);
            free(threads);
            free(args);
            break;
        }
        
        free(threads);
        free(args);
    }
    
    /* Cleanup mutexes */
    for (int i = 0; i < N; i++) {
        pthread_mutex_destroy(&node_mutexes[i]);
    }
    free(node_mutexes);
}

/* ============================================================
   Save and Print Functions
   ============================================================ */

void pagerank_save_ranks(PageRankGraph *pg, const char *filename) {
    if (!pg || !filename) return;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[PageRank] Error: Cannot open %s for writing\n", filename);
        return;
    }
    
    fprintf(f, "# PageRank Scores\n");
    fprintf(f, "# Nodes: %d\n", pg->num_nodes);
    fprintf(f, "# Damping Factor: %.2f\n", DAMPING_FACTOR);
    fprintf(f, "# Format: node_id | rank | normalized_rank(%%)\n\n");
    
    /* Find max rank for normalization */
    double max_rank = 0.0;
    for (int i = 0; i < pg->num_nodes; i++) {
        if (pg->ranks[i] > max_rank) max_rank = pg->ranks[i];
    }
    
    for (int i = 0; i < pg->num_nodes; i++) {
        double normalized = (pg->ranks[i] / max_rank) * 100.0;
        fprintf(f, "%d | %.12f | %.6f%%\n", i, pg->ranks[i], normalized);
    }
    
    fclose(f);
    printf("[PageRank] Saved ranks to %s\n", filename);
}

void pagerank_print_ranks(PageRankGraph *pg) {
    if (!pg) return;
    
    printf("\n=== PageRank Results ===\n");
    printf("Node ID | Rank (absolute) | Normalized (%%)\n");
    printf("--------|-----------------|-----------------\n");
    
    double max_rank = 0.0;
    for (int i = 0; i < pg->num_nodes; i++) {
        if (pg->ranks[i] > max_rank) max_rank = pg->ranks[i];
    }
    
    for (int i = 0; i < pg->num_nodes; i++) {
        double normalized = (pg->ranks[i] / max_rank) * 100.0;
        printf("%7d | %15.12f | %10.6f%%\n", i, pg->ranks[i], normalized);
    }
    printf("========================\n");
}

void pagerank_free(PageRankGraph *pg) {
    if (!pg) return;
    
    if (pg->outlinks) {
        for (int i = 0; i < pg->num_nodes; i++) {
            if (pg->outlinks[i]) free(pg->outlinks[i]);
        }
        free(pg->outlinks);
    }
    
    free(pg->outlink_counts);
    free(pg->ranks);
    free(pg->new_ranks);
    free(pg);
}