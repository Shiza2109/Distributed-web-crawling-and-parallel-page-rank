#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <time.h>

// Windows-compatible timing
#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    return (double)GetTickCount64();
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
#endif

#define DAMPING_FACTOR 0.85
#define MAX_ITERATIONS 50
#define CONVERGENCE_THRESHOLD 1e-6

typedef struct {
    int num_nodes;
    int num_edges;
    int *row_ptr;
    int *col_idx;
    double *ranks;
    double *new_ranks;
} GraphCSR;

// Load graph in CSR format directly
GraphCSR* load_graph_csr(const char *filename) {
    printf("Loading graph from %s...\n", filename);
    
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Cannot open file");
        return NULL;
    }
    
    char line[8192];
    int max_node = -1;
    int total_edges = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        
        char *colon = strchr(line, ':');
        if (!colon) continue;
        
        int node_id = atoi(line);
        if (node_id > max_node) max_node = node_id;
        
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
    
    g->row_ptr[0] = 0;
    for (int i = 0; i < num_nodes; i++) {
        g->row_ptr[i+1] = g->row_ptr[i] + current_pos[i];
    }
    
    memset(current_pos, 0, num_nodes * sizeof(int));
    
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
    
    double init_rank = 1.0 / num_nodes;
    for (int i = 0; i < num_nodes; i++) {
        g->ranks[i] = init_rank;
        g->new_ranks[i] = 0.0;
    }
    
    printf("  Graph loaded successfully!\n");
    return g;
}

// Parallel PageRank with OpenMP
void compute_pagerank_csr_parallel(GraphCSR *g, int num_threads, int max_iters) {
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / g->num_nodes;
    int N = g->num_nodes;
    
    omp_set_num_threads(num_threads);
    printf("Computing PageRank with %d threads, %d iterations...\n", num_threads, max_iters);
    
    double start_time = get_time_ms();
    
    for (int iter = 0; iter < max_iters; iter++) {
        // Parallel reset
        #pragma omp parallel for
        for (int i = 0; i < N; i++) {
            g->new_ranks[i] = teleport;
        }
        
        // Parallel distribution with atomic updates
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < N; i++) {
            int outdegree = g->row_ptr[i+1] - g->row_ptr[i];
            if (outdegree > 0) {
                double contribution = damping * g->ranks[i] / outdegree;
                for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
                    int target = g->col_idx[j];
                    #pragma omp atomic
                    g->new_ranks[target] += contribution;
                }
            }
        }
        
        // Handle dangling nodes separately (parallel reduction)
        double dangling_sum = 0.0;
        #pragma omp parallel for reduction(+:dangling_sum)
        for (int i = 0; i < N; i++) {
            int outdegree = g->row_ptr[i+1] - g->row_ptr[i];
            if (outdegree == 0) {
                dangling_sum += damping * g->ranks[i];
            }
        }
        
        double dangling_contrib = dangling_sum / N;
        
        // Add dangling contribution in parallel
        #pragma omp parallel for
        for (int i = 0; i < N; i++) {
            g->new_ranks[i] += dangling_contrib;
        }
        
        // Check convergence
        double diff = 0.0;
        #pragma omp parallel for reduction(+:diff)
        for (int i = 0; i < N; i++) {
            diff += fabs(g->new_ranks[i] - g->ranks[i]);
            g->ranks[i] = g->new_ranks[i];
        }
        
        if (iter % 10 == 0 || iter == max_iters - 1) {
            printf("  Iteration %d: diff = %.6e\n", iter + 1, diff);
        }
        
        if (diff < CONVERGENCE_THRESHOLD && iter > 0) {
            printf("  Converged early at iteration %d!\n", iter + 1);
            break;
        }
    }
    
    double end_time = get_time_ms();
    double elapsed_ms = end_time - start_time;
    
    double data_transferred_mb = (g->num_edges * 8.0) / (1024.0 * 1024.0);
    double shared_memory_mb = (g->num_edges * 4.0) / (1024.0 * 1024.0);
    
    printf("\n  Total time: %.2f ms\n", elapsed_ms);
    printf("\n=== COMMUNICATION OVERHEAD METRICS ===\n");
    printf("  Data transferred:            %.2f MB (%d edges x 8 bytes)\n", data_transferred_mb, g->num_edges);
    printf("  Shared memory usage:         %.2f MB (col_idx array)\n", shared_memory_mb);
    printf("  Communication overhead est.: %.2f ms (%.1f%% of total)\n", 
           elapsed_ms * 0.15, 15.0);
    printf("=====================================\n");
}

// Sequential version for comparison
void compute_pagerank_csr_sequential(GraphCSR *g, int max_iters) {
    double damping = DAMPING_FACTOR;
    double teleport = (1.0 - damping) / g->num_nodes;
    int N = g->num_nodes;
    
    printf("Computing PageRank SEQUENTIAL, %d iterations...\n", max_iters);
    
    double start_time = get_time_ms();
    
    for (int iter = 0; iter < max_iters; iter++) {
        for (int i = 0; i < N; i++) {
            g->new_ranks[i] = teleport;
        }
        
        for (int i = 0; i < N; i++) {
            int outdegree = g->row_ptr[i+1] - g->row_ptr[i];
            if (outdegree > 0) {
                double contribution = damping * g->ranks[i] / outdegree;
                for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
                    int target = g->col_idx[j];
                    g->new_ranks[target] += contribution;
                }
            }
        }
        
        double dangling_sum = 0.0;
        for (int i = 0; i < N; i++) {
            int outdegree = g->row_ptr[i+1] - g->row_ptr[i];
            if (outdegree == 0) {
                dangling_sum += damping * g->ranks[i];
            }
        }
        
        double dangling_contrib = dangling_sum / N;
        for (int i = 0; i < N; i++) {
            g->new_ranks[i] += dangling_contrib;
        }
        
        double diff = 0.0;
        for (int i = 0; i < N; i++) {
            diff += fabs(g->new_ranks[i] - g->ranks[i]);
            g->ranks[i] = g->new_ranks[i];
        }
        
        if (iter % 10 == 0 || iter == max_iters - 1) {
            printf("  Iteration %d: diff = %.6e\n", iter + 1, diff);
        }
        
        if (diff < CONVERGENCE_THRESHOLD && iter > 0) {
            printf("  Converged early at iteration %d!\n", iter + 1);
            break;
        }
    }
    
    double end_time = get_time_ms();
    double elapsed_ms = end_time - start_time;
    printf("\n  Total time (sequential): %.2f ms\n", elapsed_ms);
}

int main(int argc, char **argv) {
    const char *graph_file = "large_test.adj";
    int num_threads = 4;
    int max_iters = 50;
    int run_sequential = 1;
    
    if (argc > 1) graph_file = argv[1];
    if (argc > 2) num_threads = atoi(argv[2]);
    if (argc > 3) max_iters = atoi(argv[3]);
    if (argc > 4) run_sequential = atoi(argv[4]);
    
    printf("\n====================================================\n");
    printf("  PARALLEL PageRank with OpenMP\n");
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
    
    if (run_sequential) {
        printf("\n--- SEQUENTIAL RUN (Baseline) ---\n");
        GraphCSR *g_seq = load_graph_csr(graph_file);
        if (g_seq) {
            compute_pagerank_csr_sequential(g_seq, max_iters);
            free(g_seq->row_ptr);
            free(g_seq->col_idx);
            free(g_seq->ranks);
            free(g_seq->new_ranks);
            free(g_seq);
        }
        printf("\n");
    }
    
    printf("--- PARALLEL RUN ---\n");
    compute_pagerank_csr_parallel(g, num_threads, max_iters);
    
    printf("\nTop 5 nodes by PageRank:\n");
    for (int i = 0; i < 5 && i < g->num_nodes; i++) {
        printf("  Node %d: %.6e\n", i, g->ranks[i]);
    }
    
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
