/*
 * graph500_to_phase1_converter.c — Convert Graph500 to Phase 1 format
 *
 * Converts pre-crawled graph500-22.v/.e into phase1_graph.adj/.edge
 * Useful for testing without actual web crawling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *vertices_file = "graph500-22.v";
    const char *edges_file = "graph500-22.e";
    const char *output_adj = "phase1_graph.adj";
    const char *output_edge = "phase1_graph.edge";
    const char *output_map = "phase1_url_map.txt";

    /* Parse arguments */
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("Graph500 to Phase1 Converter\n");
        printf("\nUsage: %s [options]\n", argv[0]);
        printf("  -v FILE    Input vertices file (default: graph500-22.v)\n");
        printf("  -e FILE    Input edges file (default: graph500-22.e)\n");
        printf("  -o PREFIX  Output file prefix (default: phase1_graph)\n");
        printf("\nExample:\n");
        printf("  %s -v graph500-22.v -e graph500-22.e -o phase1_graph\n", argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            vertices_file = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            edges_file = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            char *prefix = argv[++i];
            sprintf((char*)output_adj, "%s.adj", prefix);
            sprintf((char*)output_edge, "%s.edge", prefix);
            sprintf((char*)output_map, "%s_url_map.txt", prefix);
        }
    }

    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  Graph500 to Phase1 Format Converter                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");

    printf("Input files:\n");
    printf("  Vertices: %s\n", vertices_file);
    printf("  Edges:    %s\n\n", edges_file);

    /* Load vertices */
    FILE *vfile = fopen(vertices_file, "r");
    if (!vfile) {
        fprintf(stderr, "ERROR: Cannot open %s\n", vertices_file);
        return 1;
    }

    int num_nodes = 0;
    int max_node_id = 0;
    char line[256];

    while (fgets(line, sizeof(line), vfile)) {
        int node_id = atoi(line);
        if (node_id > max_node_id) max_node_id = node_id;
        num_nodes++;
    }
    fclose(vfile);

    num_nodes = max_node_id + 1;
    printf("Detected: %d nodes\n", num_nodes);

    /* Allocate adjacency structure */
    int *out_degree = (int*)calloc(num_nodes, sizeof(int));
    int **adjacency = (int**)malloc(num_nodes * sizeof(int*));
    
    for (int i = 0; i < num_nodes; i++) {
        adjacency[i] = NULL;
    }

    /* First pass: count outgoing edges per node */
    FILE *efile = fopen(edges_file, "r");
    if (!efile) {
        fprintf(stderr, "ERROR: Cannot open %s\n", edges_file);
        free(out_degree);
        free(adjacency);
        return 1;
    }

    int num_edges = 0;
    int src, dst;

    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes && dst >= 0 && dst < num_nodes) {
            out_degree[src]++;
            num_edges++;
        }
    }

    printf("Detected: %d edges\n\n", num_edges);

    /* Allocate adjacency lists */
    for (int i = 0; i < num_nodes; i++) {
        if (out_degree[i] > 0) {
            adjacency[i] = (int*)malloc(out_degree[i] * sizeof(int));
        }
    }

    /* Second pass: populate adjacency lists */
    int *edge_idx = (int*)calloc(num_nodes, sizeof(int));
    rewind(efile);

    while (fscanf(efile, "%d %d", &src, &dst) == 2) {
        if (src >= 0 && src < num_nodes && dst >= 0 && dst < num_nodes) {
            adjacency[src][edge_idx[src]++] = dst;
        }
    }
    fclose(efile);
    free(edge_idx);

    /* Write adjacency list format (.adj) */
    printf("Writing %s...\n", output_adj);
    FILE *adj_out = fopen(output_adj, "w");
    if (!adj_out) {
        fprintf(stderr, "ERROR: Cannot write %s\n", output_adj);
        return 1;
    }

    fprintf(adj_out, "# Graph for PageRank (Adjacency List Format)\n");
    fprintf(adj_out, "# Nodes: %d\n", num_nodes);
    fprintf(adj_out, "# Edges: %d\n", num_edges);
    fprintf(adj_out, "# Format: node_id: outlink_id1,outlink_id2,...\n\n");

    for (int i = 0; i < num_nodes; i++) {
        fprintf(adj_out, "%d:", i);
        if (out_degree[i] > 0) {
            for (int j = 0; j < out_degree[i]; j++) {
                if (j > 0) fprintf(adj_out, ",");
                fprintf(adj_out, "%d", adjacency[i][j]);
            }
        }
        fprintf(adj_out, "\n");
    }
    fclose(adj_out);
    printf("  ✓ Written %d nodes, %d edges\n\n", num_nodes, num_edges);

    /* Write edge list format (.edge) */
    printf("Writing %s...\n", output_edge);
    FILE *edge_out = fopen(output_edge, "w");
    if (!edge_out) {
        fprintf(stderr, "ERROR: Cannot write %s\n", output_edge);
        return 1;
    }

    fprintf(edge_out, "# Edge list format: src dst\n");
    fprintf(edge_out, "# Total edges: %d\n\n", num_edges);

    for (int i = 0; i < num_nodes; i++) {
        for (int j = 0; j < out_degree[i]; j++) {
            fprintf(edge_out, "%d %d\n", i, adjacency[i][j]);
        }
    }
    fclose(edge_out);
    printf("  ✓ Written edge list\n\n");

    /* Write synthetic URL map */
    printf("Writing %s...\n", output_map);
    FILE *map_out = fopen(output_map, "w");
    if (!map_out) {
        fprintf(stderr, "ERROR: Cannot write %s\n", output_map);
        return 1;
    }

    fprintf(map_out, "# Synthetic URL map (Graph500 has no real URLs)\n");
    fprintf(map_out, "# Format: node_id synthetic_url\n\n");

    for (int i = 0; i < num_nodes; i++) {
        fprintf(map_out, "%d http://graph500-node-%d.synthetic/\n", i, i);
    }
    fclose(map_out);
    printf("  ✓ Written synthetic URL map for %d nodes\n\n", num_nodes);

    /* Cleanup */
    for (int i = 0; i < num_nodes; i++) {
        if (adjacency[i]) free(adjacency[i]);
    }
    free(adjacency);
    free(out_degree);

    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║  CONVERSION COMPLETE                                          ║\n");
    printf("║                                                               ║\n");
    printf("║  Files created:                                               ║\n");
    printf("║    • %s (adjacency list)                  ║\n", output_adj);
    printf("║    • %s (edge list)                       ║\n", output_edge);
    printf("║    • %s (URL map)                  ║\n", output_map);
    printf("║                                                               ║\n");
    printf("║  Ready for pipeline:                                          ║\n");
    printf("║    ./m3_integrated_pipeline.exe --max-pages %d             ║\n", num_nodes);
    printf("║    ./m3_incremental_pagerank_test.exe --threads 4            ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");

    return 0;
}
