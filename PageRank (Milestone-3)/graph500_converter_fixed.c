#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *vertices_file = "graph500-22.v";
    const char *edges_file = "graph500-22.e";
    const char *output_adj = "phase1_graph.adj";
    
    if (argc > 1) vertices_file = argv[1];
    if (argc > 2) edges_file = argv[2];
    if (argc > 3) output_adj = argv[3];
    
    printf("Converting %s and %s to %s\n", vertices_file, edges_file, output_adj);
    
    /* Count nodes */
    FILE *vfile = fopen(vertices_file, "r");
    if (!vfile) { perror("vertices file"); return 1; }
    
    int max_node = -1;
    int node;
    while (fscanf(vfile, "%d", &node) == 1) {
        if (node > max_node) max_node = node;
    }
    fclose(vfile);
    
    int num_nodes = max_node + 1;
    printf("Nodes: %d\n", num_nodes);
    
    /* Use simpler approach - just copy and convert format */
    FILE *efile = fopen(edges_file, "r");
    if (!efile) { perror("edges file"); return 1; }
    
    FILE *adj_out = fopen(output_adj, "w");
    if (!adj_out) { perror("output file"); return 1; }
    
    fprintf(adj_out, "# Graph for PageRank (Adjacency List Format)\n");
    fprintf(adj_out, "# Nodes: %d\n", num_nodes);
    fprintf(adj_out, "# Format: node_id: outlink_id1,outlink_id2,...\n\n");
    
    /* For each node, collect its outlinks */
    int *out_counts = calloc(num_nodes, sizeof(int));
    int **outlinks = malloc(num_nodes * sizeof(int*));
    
    /* First pass: count outlinks */
    int src, dst;
    int edge_count = 0;
    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes) {
            out_counts[src]++;
            edge_count++;
        }
    }
    printf("Edges: %d\n", edge_count);
    
    /* Allocate adjacency arrays */
    for (int i = 0; i < num_nodes; i++) {
        if (out_counts[i] > 0) {
            outlinks[i] = malloc(out_counts[i] * sizeof(int));
        } else {
            outlinks[i] = NULL;
        }
    }
    
    /* Second pass: fill adjacency */
    rewind(efile);
    int *counters = calloc(num_nodes, sizeof(int));
    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes && outlinks[src]) {
            outlinks[src][counters[src]++] = dst;
        }
    }
    
    /* Write adjacency file */
    for (int i = 0; i < num_nodes; i++) {
        fprintf(adj_out, "%d:", i);
        if (out_counts[i] > 0) {
            for (int j = 0; j < out_counts[i]; j++) {
                if (j > 0) fprintf(adj_out, ",");
                fprintf(adj_out, "%d", outlinks[i][j]);
            }
        }
        fprintf(adj_out, "\n");
        
        if (i % 100000 == 0) {
            printf("Processed %d nodes...\n", i);
        }
    }
    
    fclose(efile);
    fclose(adj_out);
    
    /* Cleanup */
    for (int i = 0; i < num_nodes; i++) {
        if (outlinks[i]) free(outlinks[i]);
    }
    free(outlinks);
    free(out_counts);
    free(counters);
    
    printf("Conversion complete! Output: %s\n", output_adj);
    return 0;
}
