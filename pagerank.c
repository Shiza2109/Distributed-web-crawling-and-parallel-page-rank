/* pagerank.c - Sequential PageRank only */
#include "pagerank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
    
    printf("[PageRank] First pass: counting nodes...\n");
    fflush(stdout);
    
    /* First pass: count nodes and find max node ID */
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        if (node_id > max_node_id) max_node_id = node_id;
        num_nodes++;
        
        /* Progress indicator for large files */
        if (num_nodes % 100000 == 0) {
            printf("[PageRank]   ... %d nodes found\n", num_nodes);
            fflush(stdout);
        }
    }
    
    pg->num_nodes = max_node_id + 1;
    printf("[PageRank] Total nodes: %d (max ID: %d)\n", pg->num_nodes, max_node_id);
    printf("[PageRank] Second pass: loading edges...\n");
    fflush(stdout);
    
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
    
    int nodes_processed = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        char *outlink_str = colon + 1;
        
        /* Count outlinks */
        int outlink_count = 0;
        if (outlink_str[0] != '\n' && outlink_str[0] != '\0') {
            char *saveptr;
            char *token = strtok_r(outlink_str, ",", &saveptr);
            while (token) {
                outlink_count++;
                token = strtok_r(NULL, ",", &saveptr);
            }
        }
        
        pg->outlink_counts[node_id] = outlink_count;
        nodes_processed++;
        
        if (nodes_processed % 100000 == 0) {
            printf("[PageRank]   ... processed %d nodes\n", nodes_processed);
            fflush(stdout);
        }
        
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
        }
    }
    
    fclose(f);
    
    int total_edges = 0;
    for (int i = 0; i < pg->num_nodes; i++) {
        total_edges += pg->outlink_counts[i];
    }
    
    printf("[PageRank] Loaded graph: %d nodes, %d edges\n", pg->num_nodes, total_edges);
    fflush(stdout);
    
    return pg;
}

/* ============================================================
   Sequential PageRank with Progress Indicators
   ============================================================ */

void pagerank_compute(PageRankGraph *pg) {
    if (!pg) return;
    
    int N = pg->num_nodes;
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / N;
    
    printf("[PageRank] Starting iterations\n");
    printf("  Nodes: %d\n", N);
    printf("  Damping: %.2f\n", damping);
    printf("  Threshold: %.0e\n", CONVERGENCE_THRESHOLD);
    printf("  Max iterations: %d\n", MAX_ITERATIONS);
    fflush(stdout);
    
    /* Initialize ranks to 1/N */
    printf("[PageRank] Initializing ranks...\n");
    fflush(stdout);
    
    double init_rank = 1.0 / N;
    for (int i = 0; i < N; i++) {
        pg->ranks[i] = init_rank;
        pg->new_ranks[i] = 0.0;
    }
    
    time_t start_time = time(NULL);
    time_t last_report = start_time;
    
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
        
        /* Progress report every 5 iterations or every 30 seconds */
        time_t now = time(NULL);
        int should_report = (iter % 5 == 0) || (now - last_report >= 30);
        
        if (should_report) {
            double elapsed = difftime(now, start_time);
            printf("[PageRank] Iter %4d: L1 diff = %.6e (elapsed: %.0f sec)\n", 
                   iter + 1, diff, elapsed);
            fflush(stdout);
            last_report = now;
        }
        
        if (diff < CONVERGENCE_THRESHOLD) {
            double total_elapsed = difftime(now, start_time);
            printf("[PageRank] CONVERGED after %d iterations (%.0f seconds)\n", 
                   iter + 1, total_elapsed);
            break;
        }
        
        if (iter == MAX_ITERATIONS - 1) {
            double total_elapsed = difftime(now, start_time);
            printf("[PageRank] STOPPED: Max iterations (%d) reached (%.0f seconds)\n", 
                   MAX_ITERATIONS, total_elapsed);
        }
    }
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
    
    printf("\n=== PageRank Results (first 20 nodes) ===\n");
    printf("Node ID | Rank (absolute) | Normalized (%%)\n");
    printf("--------|-----------------|-----------------\n");
    
    double max_rank = 0.0;
    int display_limit = (pg->num_nodes < 20) ? pg->num_nodes : 20;
    
    for (int i = 0; i < pg->num_nodes; i++) {
        if (pg->ranks[i] > max_rank) max_rank = pg->ranks[i];
    }
    
    for (int i = 0; i < display_limit; i++) {
        double normalized = (pg->ranks[i] / max_rank) * 100.0;
        printf("%7d | %15.12f | %10.6f%%\n", i, pg->ranks[i], normalized);
    }
    
    if (pg->num_nodes > 20) {
        printf("... and %d more nodes (see output file for full list)\n", 
               pg->num_nodes - 20);
    }
    printf("===========================================\n");
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