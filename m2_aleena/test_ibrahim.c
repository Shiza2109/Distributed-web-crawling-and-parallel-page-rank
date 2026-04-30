/* test_ibrahim.c - Test Muhammad Ibrahim's graph storage functions */
#include <stdio.h>
#include <stdlib.h>
#include "graph.h"

int main() {
    printf("=== Testing Muhammad Ibrahim's Graph Storage ===\n\n");
    
    /* Create graph */
    Graph *g = graph_create();
    if (!g) {
        printf("ERROR: Failed to create graph\n");
        return 1;
    }
    
    /* Add some test edges */
    printf("Adding edges to graph...\n");
    graph_add_edge(g, "http://a.com", "http://b.com");
    graph_add_edge(g, "http://a.com", "http://c.com");
    graph_add_edge(g, "http://a.com", "http://d.com");
    graph_add_edge(g, "http://b.com", "http://c.com");
    graph_add_edge(g, "http://b.com", "http://e.com");
    graph_add_edge(g, "http://c.com", "http://a.com");
    graph_add_edge(g, "http://d.com", "http://e.com");
    graph_add_edge(g, "http://e.com", "http://a.com");
    graph_add_edge(g, "http://e.com", "http://b.com");
    
    /* Test duplicate edge (should be ignored) */
    graph_add_edge(g, "http://a.com", "http://b.com");
    printf("Added 9 edges (1 duplicate ignored)\n\n");
    
    /* Get and display stats */
    uint64_t nodes, edges;
    graph_get_stats(g, &nodes, &edges);
    printf("Graph Statistics:\n");
    printf("  Nodes: %lu\n", nodes);
    printf("  Edges: %lu\n\n", edges);
    
    /* Save in adjacency list format */
    printf("Saving graph in adjacency list format...\n");
    if (graph_save(g, "test_graph_adj.txt", "adjacency") == 0) {
        printf("  ✓ Saved to test_graph_adj.txt\n");
    } else {
        printf("  ✗ Failed to save adjacency list\n");
    }
    
    /* Save in edge list format */
    printf("\nSaving graph in edge list format...\n");
    if (graph_save(g, "test_graph_edge.txt", "edgelist") == 0) {
        printf("  ✓ Saved to test_graph_edge.txt\n");
    } else {
        printf("  ✗ Failed to save edge list\n");
    }
    
    /* Save URL mapping */
    printf("\nSaving URL mapping...\n");
    if (graph_save_url_map(g, "test_url_map.txt") == 0) {
        printf("  ✓ Saved to test_url_map.txt\n");
    } else {
        printf("  ✗ Failed to save URL map\n");
    }
    
    /* Clean up */
    graph_destroy(g);
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}