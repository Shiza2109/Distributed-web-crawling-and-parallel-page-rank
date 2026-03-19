/* graph.h - Share this with team NOW */
#ifndef GRAPH_H
#define GRAPH_H

/* 
 * Core functions others will call:
 * - Aleena's parser output → your graph
 * - Abdur's duplicate filter → your node IDs
 */

/* Called by Shiza's worker when links are extracted */
void graph_add_edge(void *graph, const char *from_url, const char *to_url);

/* Called by Abdur to get consistent node IDs */
int graph_get_node_id(void *graph, const char *url);

/* Called at end to save for Milestone 2 */
void graph_save(void *graph, const char *filename);

#endif