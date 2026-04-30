#include "graph.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

typedef struct {
    char* url;
    int id;
    int* outlinks;  // will store neighbor IDs 
    int outlink_count;
    int outlink_capacity;
} Node;

struct Graph {
    Node* nodes;
    int node_count;
    int node_capacity;
    pthread_rwlock_t lock;  // thread safety 
};

// Helper function to count total edges (caller must hold lock) 
static uint64_t graph_count_edges_locked(Graph *g) {
    uint64_t total = 0;
    for (int i = 0; i < g->node_count; i++) {
        total += g->nodes[i].outlink_count;
    }
    return total;
}

// Initialize - Aleena's implementation
Graph* graph_create() {
    Graph* g = malloc(sizeof(Graph));
    if (!g) return NULL;
    
    g->nodes = malloc(sizeof(Node) * 1000);
    if (!g->nodes) {
        free(g);
        return NULL;
    }
    
    g->node_count = 0;
    g->node_capacity = 1000;
    pthread_rwlock_init(&g->lock, NULL);
    return g;
}


int graph_add_node(Graph* g, const char* url) {
    if (!g || !url) return -1;
    
    pthread_rwlock_wrlock(&g->lock);
    
    // Check if exists (linear search for now)
    for (int i = 0; i < g->node_count; i++) {
        if (strcasecmp(g->nodes[i].url, url) == 0) {
            pthread_rwlock_unlock(&g->lock);
            return i;
        }
    }
    
    // Resize nodes array if needed
    if (g->node_count == g->node_capacity) {
        int new_capacity = g->node_capacity * 2;
        Node *tmp = realloc(g->nodes, sizeof(Node) * new_capacity);
        if (!tmp) {
            pthread_rwlock_unlock(&g->lock);
            return -1;
        }
        g->nodes = tmp;
        g->node_capacity = new_capacity;
    }

    // Add new node
    int id = g->node_count++;
    g->nodes[id].url = strdup(url);
    if (!g->nodes[id].url) {
        g->node_count--;  /* rollback */
        pthread_rwlock_unlock(&g->lock);
        return -1;
    }
    g->nodes[id].id = id;
    g->nodes[id].outlinks = malloc(sizeof(int) * 10);
    if (!g->nodes[id].outlinks) {
        free(g->nodes[id].url);
        g->node_count--;
        pthread_rwlock_unlock(&g->lock);
        return -1;
    }
    g->nodes[id].outlink_count = 0;
    g->nodes[id].outlink_capacity = 10;
    
    pthread_rwlock_unlock(&g->lock);
    return id;
}

// Add edge
void graph_add_edge(void *graph, const char *from_url, const char *to_url) {
    Graph *g = (Graph *)graph;
    if (!g || !from_url || !to_url) return;

    // Get IDs for both nodes
    int from_id = graph_add_node(g, from_url);
    int to_id = graph_add_node(g, to_url);
    
    if (from_id < 0 || to_id < 0) return;

    pthread_rwlock_wrlock(&g->lock);
    Node *from_node = &g->nodes[from_id];

    // Check for duplicate edge
    for (int i = 0; i < from_node->outlink_count; i++) {
        if (from_node->outlinks[i] == to_id) {
            pthread_rwlock_unlock(&g->lock);
            return;
        }
    }

    // Resize (if needed)
    if (from_node->outlink_count == from_node->outlink_capacity) {
        int new_capacity = from_node->outlink_capacity * 2;
        int *tmp = realloc(from_node->outlinks, sizeof(int) * new_capacity);
        if (!tmp) {
            pthread_rwlock_unlock(&g->lock);
            return;
        }
        from_node->outlinks = tmp;
        from_node->outlink_capacity = new_capacity;
    }

    // Add the edge
    from_node->outlinks[from_node->outlink_count++] = to_id;
    pthread_rwlock_unlock(&g->lock);
}

// Get node ID - Abdur's integration point
int graph_get_node_id(void *graph, const char *url) {
    Graph *g = (Graph *)graph;
    if (!g || !url) return -1;

    pthread_rwlock_rdlock(&g->lock);
    
    for (int i = 0; i < g->node_count; i++) {
        if (strcasecmp(g->nodes[i].url, url) == 0) {
            pthread_rwlock_unlock(&g->lock);
            return i;
        }
    }
    
    pthread_rwlock_unlock(&g->lock);
    return -1;
}


// Save graph in adjacency list or edge list format 
int graph_save(void *graph, const char *filename, const char *format) {
    Graph *g = (Graph *)graph;
    if (!g || !filename || !format) return -1;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[Graph] Error: Cannot open %s for writing\n", filename);
        return -1;
    }
    
    pthread_rwlock_rdlock(&g->lock);
    
    if (strcmp(format, "adjacency") == 0) {
        // Format: node_id: outlink1,outlink2,outlink3...
        fprintf(f, "# Graph for PageRank (Adjacency List Format)\n");
        fprintf(f, "# Nodes: %d\n", g->node_count);
        fprintf(f, "# Edges: %lu\n", graph_count_edges_locked(g));
        fprintf(f, "# Format: node_id: outlink_id1,outlink_id2,...\n\n");
        
        for (int i = 0; i < g->node_count; i++) {
            fprintf(f, "%d:", i);
            
            Node *node = &g->nodes[i];
            for (int j = 0; j < node->outlink_count; j++) {
                if (j > 0) fprintf(f, ",");
                fprintf(f, "%d", node->outlinks[j]);
            }
            fprintf(f, "\n");
        }
    } 
    else if (strcmp(format, "edgelist") == 0) {
        // Format: source_id target_id 
        fprintf(f, "# Graph for PageRank (Edge List Format)\n");
        fprintf(f, "# Nodes: %d\n", g->node_count);
        fprintf(f, "# Edges: %lu\n", graph_count_edges_locked(g));
        fprintf(f, "# Format: source_id target_id\n\n");
        
        for (int i = 0; i < g->node_count; i++) {
            Node *node = &g->nodes[i];
            for (int j = 0; j < node->outlink_count; j++) {
                fprintf(f, "%d %d\n", i, node->outlinks[j]);
            }
        }
    }
    else {
        fprintf(stderr, "[Graph] Error: Unknown format '%s'\n", format);
        pthread_rwlock_unlock(&g->lock);
        fclose(f);
        return -1;
    }
    
    uint64_t edge_count = graph_count_edges_locked(g);
    pthread_rwlock_unlock(&g->lock);
    fclose(f);
    
    printf("[Graph] Saved %d nodes, %lu edges to %s (%s format)\n", 
           g->node_count, edge_count, filename, format);
    return 0;
}

// Save URL-to-ID mapping for debugging and reproduction 
int graph_save_url_map(void *graph, const char *filename) {
    Graph *g = (Graph *)graph;
    if (!g || !filename) return -1;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[Graph] Error: Cannot open %s for writing\n", filename);
        return -1;
    }
    
    pthread_rwlock_rdlock(&g->lock);
    
    fprintf(f, "# URL to ID Mapping\n");
    fprintf(f, "# Total nodes: %d\n", g->node_count);
    fprintf(f, "# Format: node_id: url\n\n");
    
    for (int i = 0; i < g->node_count; i++) {
        fprintf(f, "%d: %s\n", i, g->nodes[i].url);
    }
    
    pthread_rwlock_unlock(&g->lock);
    fclose(f);
    
    printf("[Graph] Saved URL map (%d entries) to %s\n", g->node_count, filename);
    return 0;
}

// Get graph stats
void graph_get_stats(void *graph, uint64_t *out_nodes, uint64_t *out_edges) {
    Graph *g = (Graph *)graph;
    if (!g) {
        if (out_nodes) *out_nodes = 0;
        if (out_edges) *out_edges = 0;
        return;
    }
    
    pthread_rwlock_rdlock(&g->lock);
    if (out_nodes) *out_nodes = (uint64_t)g->node_count;
    if (out_edges) {
        *out_edges = graph_count_edges_locked(g);
    }
    pthread_rwlock_unlock(&g->lock);
}

// Free all graph resources
void graph_destroy(void *graph) {
    Graph *g = (Graph *)graph;
    if (!g) return;
    
    pthread_rwlock_wrlock(&g->lock);
    
    for (int i = 0; i < g->node_count; i++) {
        free(g->nodes[i].url);
        free(g->nodes[i].outlinks);
    }
    free(g->nodes);
    
    pthread_rwlock_unlock(&g->lock);
    pthread_rwlock_destroy(&g->lock);
    free(g);
}