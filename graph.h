#ifndef GRAPH_H
#define GRAPH_H

#include <stdint.h>
#include <stdio.h>
#include "frontier.h"

// Forward declaration
typedef struct Graph Graph;

// Core graph operations (Aleena's implementation)
Graph* graph_create(void);
int graph_add_node(void *graph, const char *url);
void graph_add_edge(void *graph, const char *from_url, const char *to_url);
int graph_get_node_id(void *graph, const char *url);

// Muhammad Ibrahim's functions for Milestone 1
/*
 * graph_save - Save graph in PageRank-friendly format
 * @graph: graph instance
 * @filename: output file path
 * @format: "adjacency" or "edgelist"
 * 
 * Returns 0 on success, -1 on error
 */
int graph_save(void *graph, const char *filename, const char *format);

/*
 * graph_save_url_map - Save URL-to-ID mapping for debugging/reproducibility
 * @graph: graph instance
 * @filename: output file path
 * 
 * Returns 0 on success, -1 on error
 */
int graph_save_url_map(void *graph, const char *filename);

/*
 * graph_get_stats - Get graph statistics
 * @graph: graph instance
 * @out_nodes: pointer to store node count (can be NULL)
 * @out_edges: pointer to store edge count (can be NULL)
 */
void graph_get_stats(void *graph, uint64_t *out_nodes, uint64_t *out_edges);

/*
 * graph_destroy - Free all graph resources
 * @graph: graph instance
 */
void graph_destroy(void *graph);

#endif