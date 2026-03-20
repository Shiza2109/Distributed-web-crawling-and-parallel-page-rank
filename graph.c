/* graph.c - Start implementing NOW */
#include "graph.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    char* url;
    int id;
    int* outlinks;  /* will store neighbor IDs */
    int outlink_count;
    int outlink_capacity;
} Node;

typedef struct {
    Node* nodes;
    int node_count;
    int node_capacity;
    pthread_rwlock_t lock;  /* thread safety */
} Graph;

/* Initialize - can do now */
Graph* graph_create() {
    Graph* g = malloc(sizeof(Graph));
    g->nodes = malloc(sizeof(Node) * 1000);
    g->node_count = 0;
    g->node_capacity = 1000;
    pthread_rwlock_init(&g->lock, NULL);
    return g;
}

/* Add node - can implement basic version now */
int graph_add_node(Graph* g, const char* url) {
    /* Basic implementation - will integrate with Abdur's duplicate filter later */
    pthread_rwlock_wrlock(&g->lock);
    
    /* Check if exists (simple linear search for now) */
    for (int i = 0; i < g->node_count; i++) {
        if (strcmp(g->nodes[i].url, url) == 0) {
            pthread_rwlock_unlock(&g->lock);
            return i;
        }
    }
    
    /* Add new node (resize array if needed) */
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

    int id = g->node_count++;
    g->nodes[id].url = strdup(url);
    g->nodes[id].id = id;
    g->nodes[id].outlinks = malloc(sizeof(int) * 10);
    g->nodes[id].outlink_count = 0;
    g->nodes[id].outlink_capacity = 10;
    
    pthread_rwlock_unlock(&g->lock);
    return id;
}


void graph_add_edge(void *graph, const char *from_url, const char *to_url) {
    Graph *g = (Graph *)graph;
    if (!g || !from_url || !to_url) return;

    // Get IDs for both nodes 
    int from_id = graph_add_node(g, from_url);
    int to_id = graph_add_node(g, to_url);

    pthread_rwlock_wrlock(&g->lock);
    Node *from_node = &g->nodes[from_id];

    // Check for duplicate edge 
    for (int i = 0; i < from_node->outlink_count; i++) {
        if (from_node->outlinks[i] == to_id) {
            pthread_rwlock_unlock(&g->lock);
            return;
        }
    }

    /* Resize if needed (safe realloc) */
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

    // Add the edge (ID of the destination node)
    from_node->outlinks[from_node->outlink_count++] = to_id;
    pthread_rwlock_unlock(&g->lock);
}


// Abdur Rehman: Ensure duplicate URLs are filtered 
int graph_get_node_id(void *graph, const char *url) {
    Graph *g = (Graph *)graph;
    if (!g || !url) return -1;

    pthread_rwlock_rdlock(&g->lock);
    
    // Basic implementation
    for (int i = 0; i < g->node_count; i++) {
        if (strcmp(g->nodes[i].url, url) == 0) {
            pthread_rwlock_unlock(&g->lock);
            return i;
        }
    }
    
    pthread_rwlock_unlock(&g->lock);
    return -1; 
}

/* Ibrahim: Store the resulting graph in a format suitable for PageRank
//void graph_save