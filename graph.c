/* graph.c - Start implementing NOW */
#include "graph.h"
#include <stdio.h>
#include <string.h>
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
    
    /* Add new node */
    int id = g->node_count++;
    g->nodes[id].url = strdup(url);
    g->nodes[id].id = id;
    g->nodes[id].outlinks = malloc(sizeof(int) * 10);
    g->nodes[id].outlink_count = 0;
    g->nodes[id].outlink_capacity = 10;
    
    pthread_rwlock_unlock(&g->lock);
    return id;
}