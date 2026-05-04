#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "parallel_pagerank.h"

typedef ParallelGraph Graph;

Graph* parallel_allocate_graph(int num_nodes) {
    Graph *g = (Graph*)malloc(sizeof(Graph));
    g->num_nodes = num_nodes;
    g->num_edges = 0;
    g->num_threads = 4;  /* default */
    g->out_degree = (int*)calloc(num_nodes, sizeof(int));
    g->adjacency = (int**)malloc(num_nodes * sizeof(int*));
    g->current_rank = (double*)malloc(num_nodes * sizeof(double));
    g->next_rank = (double*)malloc(num_nodes * sizeof(double));
    g->rank_locks = (pthread_mutex_t*)malloc(num_nodes * sizeof(pthread_mutex_t));
    
    /* Initialize adjacency list pointers and locks */
    for (int i = 0; i < num_nodes; i++) {
        g->adjacency[i] = NULL;
        pthread_mutex_init(&g->rank_locks[i], NULL);
    }
    
    return g;
}

void parallel_free_graph(Graph *g) {
    if (!g) return;
    
    for (int i = 0; i < g->num_nodes; i++) {
        if (g->adjacency[i]) free(g->adjacency[i]);
        pthread_mutex_destroy(&g->rank_locks[i]);
    }
    
    free(g->adjacency);
    free(g->out_degree);
    free(g->current_rank);
    free(g->next_rank);
    free(g->rank_locks);
    free(g);
}

/* Load graph from adjacency file (.adj format) */
Graph* parallel_load_graph(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[ParallelPageRank] Error: Cannot open %s\n", filename);
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
    printf("[ParallelPageRank] Loading graph with %d nodes from %s...\n", num_nodes, filename);
    
    Graph *g = parallel_allocate_graph(num_nodes);

    /* Second pass: load edges */
    rewind(f);
    int *edge_count = (int*)calloc(num_nodes, sizeof(int));

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        int node_id = atoi(line);
        
        /* Count outlinks for this node */
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
        
        /* Parse outlinks */
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
        g->current_rank[i] = initial_rank;
        g->next_rank[i] = 0.0;
    }

    printf("[ParallelPageRank] Loaded: %d nodes, %d edges\n\n", g->num_nodes, g->num_edges);
    return g;
}

typedef struct {
    int start_node;
    int end_node;
    Graph *graph;
    double local_delta;
} WorkerArgs;

void* reset_ranks_worker(void *args) {
    WorkerArgs *work = (WorkerArgs*)args;
    Graph *g = work->graph;
    double base_rank = (1.0 - DAMPING_FACTOR) / g->num_nodes;
    
    for (int i = work->start_node; i < work->end_node; i++) {
        g->next_rank[i] = base_rank;
    }
    
    free(work);
    return NULL;
}

void* distribute_ranks_worker(void *args) {
    WorkerArgs *work = (WorkerArgs*)args;
    Graph *g = work->graph;
    
    for (int source = work->start_node; source < work->end_node; source++) {
        if (g->out_degree[source] > 0) {
            double contribution = g->current_rank[source] / g->out_degree[source];
            
            for (int i = 0; i < g->out_degree[source]; i++) {
                int target = g->adjacency[source][i];
                if (target >= 0 && target < g->num_nodes) {
                    pthread_mutex_lock(&g->rank_locks[target]);
                    g->next_rank[target] += DAMPING_FACTOR * contribution;
                    pthread_mutex_unlock(&g->rank_locks[target]);
                }
            }
        }
    }
    
    free(work);
    return NULL;
}

void* compute_delta_worker(void *args) {
    WorkerArgs *work = (WorkerArgs*)args;
    Graph *g = work->graph;
    double local_delta = 0.0;
    
    for (int i = work->start_node; i < work->end_node; i++) {
        local_delta += fabs(g->next_rank[i] - g->current_rank[i]);
        g->current_rank[i] = g->next_rank[i];
    }
    
    work->local_delta = local_delta;
    return NULL;
}

double compute_iteration(Graph *g, int num_threads) {
    pthread_t *threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    WorkerArgs **args = (WorkerArgs**)malloc(num_threads * sizeof(WorkerArgs*));
    int nodes_per_thread = (g->num_nodes + num_threads - 1) / num_threads;
    
    /* Phase 1: Reset ranks */
    for (int t = 0; t < num_threads; t++) {
        args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        args[t]->start_node = t * nodes_per_thread;
        args[t]->end_node = (t + 1) * nodes_per_thread;
        if (args[t]->end_node > g->num_nodes) args[t]->end_node = g->num_nodes;
        args[t]->graph = g;
        args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, reset_ranks_worker, args[t]);
    }
    
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    /* Phase 2: Distribute ranks */
    for (int t = 0; t < num_threads; t++) {
        args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        args[t]->start_node = t * nodes_per_thread;
        args[t]->end_node = (t + 1) * nodes_per_thread;
        if (args[t]->end_node > g->num_nodes) args[t]->end_node = g->num_nodes;
        args[t]->graph = g;
        args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, distribute_ranks_worker, args[t]);
    }
    
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    
    /* Phase 3: Compute delta and swap */
    WorkerArgs **worker_args = (WorkerArgs**)malloc(num_threads * sizeof(WorkerArgs*));
    for (int t = 0; t < num_threads; t++) {
        worker_args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        worker_args[t]->start_node = t * nodes_per_thread;
        worker_args[t]->end_node = (t + 1) * nodes_per_thread;
        if (worker_args[t]->end_node > g->num_nodes) worker_args[t]->end_node = g->num_nodes;
        worker_args[t]->graph = g;
        worker_args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, compute_delta_worker, worker_args[t]);
    }
    
    double total_delta = 0.0;
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
        total_delta += worker_args[t]->local_delta;
        free(worker_args[t]);
    }
    
    free(worker_args);
    free(threads);
    free(args);
    
    return total_delta;
}

PageRankResult parallel_pagerank_compute(Graph *g, int num_threads) {
    g->num_threads = num_threads;
    
    printf("\n========================================================\n");
    printf("  PARALLEL PAGERANK COMPUTATION\n");
    printf("========================================================\n");
    printf("  Threads              : %d\n", num_threads);
    printf("  Damping Factor       : %.2f\n", DAMPING_FACTOR);
    printf("  Convergence Threshold: %.0e\n", CONVERGENCE_THRESHOLD);
    printf("========================================================\n\n");
    
    int iter = 0;
    PageRankResult result = {0, 0.0};
    while (1) {
        double delta = compute_iteration(g, num_threads);
        
        printf("  Iteration %3d: delta=%e\n", iter, delta);
        
        if (delta < CONVERGENCE_THRESHOLD) {
            printf("\n  Converged at iteration %d\n", iter);
            printf("========================================================\n\n");
            result.iterations = iter + 1;
            result.final_delta = delta;
            break;
        }
        
        iter++;
    }
    return result;
}

void parallel_pagerank_save_ranks(Graph *g, const char *output_file) {
    FILE *file = fopen(output_file, "w");
    if (!file) {
        perror("Error opening output file");
        return;
    }
    
    for (int i = 0; i < g->num_nodes; i++) {
        fprintf(file, "%d %.15e\n", i, g->current_rank[i]);
    }
    
    fclose(file);
    printf("[ParallelPageRank] Results saved to %s\n", output_file);
}

void parallel_pagerank_print_ranks(Graph *g) {
    printf("[ParallelPageRank] Printing top 10 nodes by PageRank:\n\n");
    
    /* Create array of (node_id, rank) pairs */
    typedef struct {
        int node_id;
        double rank;
    } NodeRank;
    
    NodeRank *nodes = (NodeRank*)malloc(g->num_nodes * sizeof(NodeRank));
    for (int i = 0; i < g->num_nodes; i++) {
        nodes[i].node_id = i;
        nodes[i].rank = g->current_rank[i];
    }
    
    /* Simple selection sort for top 10 */
    int k = (g->num_nodes < 10) ? g->num_nodes : 10;
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

/* Standalone test main (optional) */
#ifdef PARALLEL_PAGERANK_STANDALONE
int main() {
    printf("\n===== PARALLEL PAGERANK STANDALONE TEST =====\n");
    
    Graph *g = parallel_load_graph("phase1_graph.adj");
    
    if (!g) {
        fprintf(stderr, "Failed to load graph\n");
        return 1;
    }
    
    parallel_pagerank_compute(g, 4);
    parallel_pagerank_print_ranks(g);
    parallel_pagerank_save_ranks(g, "pagerank_output.txt");
    
    parallel_free_graph(g);
    
    printf("===== TEST COMPLETE =====\n\n");
    return 0;
}
#endif