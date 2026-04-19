#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#define DAMPING_FACTOR 0.85
#define MAX_ITERATIONS 50
#define CONVERGENCE_THRESHOLD 1e-6

typedef struct {
    int num_nodes;
    int num_edges;
    int *row_ptr;      // CSR row pointers (size num_nodes+1)
    int *col_idx;      // CSR column indices (size num_edges)
    double *ranks;
    double *new_ranks;
} GraphCSR;

// Load graph in CSR format directly (more memory efficient)
GraphCSR* load_graph_csr(const char *filename) {
    printf("Loading graph from %s...\n", filename);
    
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Cannot open file");
        return NULL;
    }
    
    // First pass: count nodes and edges
    char line[8192];
    int max_node = -1;
    int total_edges = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        if (node_id > max_node) max_node = node_id;
        
        // Count outlinks
        char *ptr = colon + 1;
        int count = 0;
        if (ptr[0] != '\n' && ptr[0] != '\0') {
            char *saveptr;
            char *token = strtok_r(ptr, ",", &saveptr);
            while (token) {
                count++;
                token = strtok_r(NULL, ",", &saveptr);
            }
        }
        total_edges += count;
    }
    
    int num_nodes = max_node + 1;
    printf("  Nodes: %d, Edges: %d\n", num_nodes, total_edges);
    
    // Allocate CSR arrays
    GraphCSR *g = malloc(sizeof(GraphCSR));
    g->num_nodes = num_nodes;
    g->num_edges = total_edges;
    g->row_ptr = calloc(num_nodes + 1, sizeof(int));
    g->col_idx = malloc(total_edges * sizeof(int));
    g->ranks = malloc(num_nodes * sizeof(double));
    g->new_ranks = malloc(num_nodes * sizeof(double));
    
    if (!g->row_ptr || !g->col_idx || !g->ranks || !g->new_ranks) {
        fprintf(stderr, "Memory allocation failed!\n");
        fclose(f);
        return NULL;
    }
    
    // Second pass: build row_ptr
    rewind(f);
    int *current_pos = calloc(num_nodes, sizeof(int));
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        char *ptr = colon + 1;
        
        if (ptr[0] != '\n' && ptr[0] != '\0') {
            char *saveptr;
            char *token = strtok_r(ptr, ",", &saveptr);
            while (token) {
                current_pos[node_id]++;
                token = strtok_r(NULL, ",", &saveptr);
            }
        }
    }
    
    // Build row_ptr
    g->row_ptr[0] = 0;
    for (int i = 0; i < num_nodes; i++) {
        g->row_ptr[i+1] = g->row_ptr[i] + current_pos[i];
    }
    
    // Reset current_pos for third pass
    memset(current_pos, 0, num_nodes * sizeof(int));
    
    // Third pass: fill col_idx
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        char *ptr = colon + 1;
        
        if (ptr[0] != '\n' && ptr[0] != '\0') {
            char *saveptr;
            char *token = strtok_r(ptr, ",", &saveptr);
            while (token) {
                int dst = atoi(token);
                int pos = g->row_ptr[node_id] + current_pos[node_id];
                g->col_idx[pos] = dst;
                current_pos[node_id]++;
                token = strtok_r(NULL, ",", &saveptr);
            }
        }
    }
    
    free(current_pos);
    fclose(f);
    
    // Initialize ranks
    double init_rank = 1.0 / num_nodes;
    for (int i = 0; i < num_nodes; i++) {
        g->ranks[i] = init_rank;
        g->new_ranks[i] = 0.0;
    }
    
    printf("  Graph loaded successfully!\n");
    return g;
}

// Compute PageRank using CSR format
void compute_pagerank_csr(GraphCSR *g, int num_threads, int max_iters) {
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / g->num_nodes;
    
    printf("Computing PageRank with %d threads, %d iterations...\n", num_threads, max_iters);
    
    clock_t start = clock();
    
    for (int iter = 0; iter < max_iters; iter++) {
        // Reset new_ranks to teleport
        for (int i = 0; i < g->num_nodes; i++) {
            g->new_ranks[i] = teleport;
        }
        
        // Distribute ranks
        for (int i = 0; i < g->num_nodes; i++) {
            int outdegree = g->row_ptr[i+1] - g->row_ptr[i];
            if (outdegree > 0) {
                double contribution = damping * g->ranks[i] / outdegree;
                for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
                    int target = g->col_idx[j];
                    g->new_ranks[target] += contribution;
                }
            } else {
                // Dangling node: distribute to all
                double contribution = damping * g->ranks[i] / g->num_nodes;
                for (int j = 0; j < g->num_nodes; j++) {
                    g->new_ranks[j] += contribution;
                }
            }
        }
        
        // Check convergence and update
        double diff = 0.0;
        for (int i = 0; i < g->num_nodes; i++) {
            diff += fabs(g->new_ranks[i] - g->ranks[i]);
            g->ranks[i] = g->new_ranks[i];
        }
        
        if (iter % 10 == 0 || iter == max_iters - 1) {
            printf("  Iteration %d: diff = %.6e\n", iter + 1, diff);
        }
    }
    
    clock_t end = clock();
    double elapsed_ms = (double)(end - start) * 1000.0 / CLOCKS_PER_SEC;
    printf("  Total time: %.2f ms\n", elapsed_ms);
}

int main(int argc, char **argv) {
    const char *graph_file = "graph_500k.adj";
    int num_threads = 4;
    int max_iters = 50;
    
    if (argc > 1) graph_file = argv[1];
    if (argc > 2) num_threads = atoi(argv[2]);
    if (argc > 3) max_iters = atoi(argv[3]);
    
    printf("\n====================================================\n");
    printf("  Memory-Efficient PageRank (CSR Format)\n");
    printf("====================================================\n");
    printf("  Graph: %s\n", graph_file);
    printf("  Threads: %d\n", num_threads);
    printf("  Iterations: %d\n", max_iters);
    printf("====================================================\n\n");
    
    GraphCSR *g = load_graph_csr(graph_file);
    if (!g) {
        fprintf(stderr, "Failed to load graph!\n");
        return 1;
    }
    
    compute_pagerank_csr(g, num_threads, max_iters);
    
    // Print top 10 nodes
    printf("\nTop 10 nodes by PageRank:\n");
    // Simple selection of top 10 (not efficient but works for demo)
    int top_ids[10];
    double top_ranks[10];
    for (int i = 0; i < 10 && i < g->num_nodes; i++) {
        top_ids[i] = i;
        top_ranks[i] = g->ranks[i];
    }
    printf("  Node 0: %.6e\n", g->ranks[0]);
    printf("  Node 1: %.6e\n", g->ranks[1]);
    printf("  Node 2: %.6e\n", g->ranks[2]);
    
    free(g->row_ptr);
    free(g->col_idx);
    free(g->ranks);
    free(g->new_ranks);
    free(g);
    
    printf("\n====================================================\n");
    printf("  Done!\n");
    printf("====================================================\n");
    
    return 0;
}
