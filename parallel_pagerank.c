#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define DAMPING_FACTOR 0.85
#define CONVERGENCE_THRESHOLD 1e-10
#define NUM_THREADS 4

typedef struct {
    int num_nodes;
    int num_edges;
    int *out_degree;           // Number of outlinks per node
    int **adjacency;           // Adjacency list
    double *current_rank;      // Current PageRank values
    double *next_rank;         // Next iteration PageRank values
    pthread_mutex_t *rank_locks;  // Locks for rank updates
} Graph;

Graph* allocate_graph(int num_nodes) {
    Graph *g = (Graph*)malloc(sizeof(Graph));
    g->num_nodes = num_nodes;
    g->num_edges = 0;
    g->out_degree = (int*)calloc(num_nodes, sizeof(int));
    g->adjacency = (int**)malloc(num_nodes * sizeof(int*));
    g->current_rank = (double*)malloc(num_nodes * sizeof(double));
    g->next_rank = (double*)malloc(num_nodes * sizeof(double));
    g->rank_locks = (pthread_mutex_t*)malloc(num_nodes * sizeof(pthread_mutex_t));
    
    // Initialize adjacency list pointers and locks
    for (int i = 0; i < num_nodes; i++) {
        g->adjacency[i] = NULL;
        pthread_mutex_init(&g->rank_locks[i], NULL);
    }
    
    return g;
}

void free_graph(Graph *g) {
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

Graph* load_graph(const char *v_file, const char *e_file) {
    // Load vertices from .v file
    FILE *vfile = fopen(v_file, "r");
    if (!vfile) {
        perror("Error opening .v file");
        return NULL;
    }
    
    int num_nodes = 0;
    int max_node_id = 0;
    char line[256];
    
    // Count nodes and find max node ID
    while (fgets(line, sizeof(line), vfile)) {
        int node_id = atoi(line);
        if (node_id > max_node_id) {
            max_node_id = node_id;
        }
        num_nodes++;
    }
    
    num_nodes = max_node_id + 1;
    printf("Loading graph with %d nodes...\n", num_nodes);
    Graph *g = allocate_graph(num_nodes);
    
    // Load edges from text .e file (format: src dst)
    FILE *efile = fopen(e_file, "r");
    if (!efile) {
        perror("Error opening .e file");
        fclose(vfile);
        free_graph(g);
        return NULL;
    }
    
    // First pass: count outgoing edges per node
    int src, dst;
    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes && dst >= 0 && dst < num_nodes) {
            g->out_degree[src]++;
        }
    }
    
    rewind(efile);
    
    // Allocate adjacency lists
    for (int i = 0; i < num_nodes; i++) {
        if (g->out_degree[i] > 0) {
            g->adjacency[i] = (int*)malloc(g->out_degree[i] * sizeof(int));
        }
    }
    
    // Reset counters for second pass
    int *edge_count = (int*)calloc(num_nodes, sizeof(int));
    
    // Second pass: populate adjacency lists
    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes && dst >= 0 && dst < num_nodes) {
            int idx = edge_count[src];
            g->adjacency[src][idx] = dst;
            edge_count[src]++;
            g->num_edges++;
        }
    }
    
    free(edge_count);
    
    fclose(vfile);
    fclose(efile);
    
    // Initialize ranks uniformly
    double initial_rank = 1.0 / g->num_nodes;
    for (int i = 0; i < g->num_nodes; i++) {
        g->current_rank[i] = initial_rank;
        g->next_rank[i] = 0.0;
    }
    
    printf("Loaded: %d nodes, %d edges\n\n", g->num_nodes, g->num_edges);
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

double compute_iteration(Graph *g) {
    pthread_t threads[NUM_THREADS];
    WorkerArgs *args[NUM_THREADS];
    int nodes_per_thread = (g->num_nodes + NUM_THREADS - 1) / NUM_THREADS;
    
    // Phase 1: Reset ranks
    for (int t = 0; t < NUM_THREADS; t++) {
        args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        args[t]->start_node = t * nodes_per_thread;
        args[t]->end_node = (t + 1) * nodes_per_thread;
        if (args[t]->end_node > g->num_nodes) args[t]->end_node = g->num_nodes;
        args[t]->graph = g;
        args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, reset_ranks_worker, args[t]);
    }
    
    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }
    
    // Phase 2: Distribute ranks
    for (int t = 0; t < NUM_THREADS; t++) {
        args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        args[t]->start_node = t * nodes_per_thread;
        args[t]->end_node = (t + 1) * nodes_per_thread;
        if (args[t]->end_node > g->num_nodes) args[t]->end_node = g->num_nodes;
        args[t]->graph = g;
        args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, distribute_ranks_worker, args[t]);
    }
    
    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }
    
    // Phase 3: Compute delta and swap
    WorkerArgs *worker_args[NUM_THREADS];
    for (int t = 0; t < NUM_THREADS; t++) {
        worker_args[t] = (WorkerArgs*)malloc(sizeof(WorkerArgs));
        worker_args[t]->start_node = t * nodes_per_thread;
        worker_args[t]->end_node = (t + 1) * nodes_per_thread;
        if (worker_args[t]->end_node > g->num_nodes) worker_args[t]->end_node = g->num_nodes;
        worker_args[t]->graph = g;
        worker_args[t]->local_delta = 0.0;
        
        pthread_create(&threads[t], NULL, compute_delta_worker, worker_args[t]);
    }
    
    double total_delta = 0.0;
    for (int t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
        total_delta += worker_args[t]->local_delta;
        free(worker_args[t]);
    }
    
    return total_delta;
}

void run_pagerank(Graph *g) {
    printf("\n===== STARTING PAGERANK COMPUTATION =====");
    printf("\nRunning Parallel PageRank (damping=%.2f, convergence=%.0e)\n", 
           DAMPING_FACTOR, CONVERGENCE_THRESHOLD);
    printf("Threads: %d\n\n", NUM_THREADS);
    
    int iter = 0;
    while (1) {
        double delta = compute_iteration(g);
        
        printf("Iteration %2d: delta=%e\n", iter, delta);
        
        if (delta < CONVERGENCE_THRESHOLD) {
            printf("Converged at iteration %d\n", iter);
            printf("===== PAGERANK COMPUTATION COMPLETE =====");
            printf("\n\n");
            break;
        }
        
        iter++;
    }
}

void save_results(Graph *g, const char *output_file) {
    FILE *file = fopen(output_file, "w");
    if (!file) {
        perror("Error opening output file");
        return;
    }
    
    fprintf(file, "node_id,pagerank\n");
    
    for (int i = 0; i < g->num_nodes; i++) {
        fprintf(file, "%d,%.6e\n", i, g->current_rank[i]);
    }
    
    fclose(file);
    printf("Results saved to %s\n", output_file);
}

void print_top_nodes(Graph *g, int k) {
    printf("\nTop %d nodes by PageRank:\n", k);
    
    // Create array of (node_id, rank) pairs
    typedef struct {
        int node_id;
        double rank;
    } NodeRank;
    
    NodeRank *nodes = (NodeRank*)malloc(g->num_nodes * sizeof(NodeRank));
    for (int i = 0; i < g->num_nodes; i++) {
        nodes[i].node_id = i;
        nodes[i].rank = g->current_rank[i];
    }
    
    // Simple bubble sort for top k (fast enough for small k)
    for (int i = 0; i < k && i < g->num_nodes; i++) {
        for (int j = i + 1; j < g->num_nodes; j++) {
            if (nodes[j].rank > nodes[i].rank) {
                NodeRank temp = nodes[i];
                nodes[i] = nodes[j];
                nodes[j] = temp;
            }
        }
    }
    
    for (int i = 0; i < k && i < g->num_nodes; i++) {
        printf("  %d. Node %d: %.6e\n", i + 1, nodes[i].node_id, nodes[i].rank);
    }
    
    free(nodes);
}

int main() {
    printf("\n===== PROGRAM START =====");
    printf("\n");
    Graph *g = load_graph("graph500-22.v", "graph500-22.e");
    
    if (!g) {
        fprintf(stderr, "Failed to load graph\n");
        return 1;
    }
    
    run_pagerank(g);
    save_results(g, "pagerank_output.csv");
    
    free_graph(g);
    
    printf("\n===== PROGRAM END =====");
    printf("\n\n");
    return 0;
}
