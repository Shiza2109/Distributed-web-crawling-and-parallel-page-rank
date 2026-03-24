#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include "frontier.h"
#include "crawl_policy.h"
#include "worker.h"
#include "graph.h"

// Windows compatibility
#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep((x)*1000)
#endif

int main() {
    printf("========================================\n");
    printf("  Milestone 1 - Complete Integration Test\n");
    printf("========================================\n\n");
    
    // 1. Create crawl policy (Abdur's code) 
    printf("1. Creating crawl policy...\n");
    CrawlPolicy *policy = policy_create(2,     // max_depth = 2 
                                        100,   // max_pages = 100 
                                        0);    // same_domain_only = 0 (crawl anywhere)
    if (!policy) {
        printf("ERROR: Failed to create policy\n");
        return 1;
    }
    printf("   ✓ Policy created (max_depth=2, max_pages=100)\n\n");
    
    // 2. Initialize frontier (Abdur's code) 
    printf("2. Initializing frontier with duplicate filtering...\n");
    Frontier frontier;
    frontier_init(&frontier, policy);
    printf("   ✓ Frontier initialized\n\n");
    
    // 3. Create graph  
    printf("3. Creating graph for PageRank...\n");
    Graph *graph = graph_create();
    if (!graph) {
        printf("ERROR: Failed to create graph\n");
        frontier_destroy(&frontier);
        return 1;
    }
    printf("   ✓ Graph created\n\n");
    
    // 4. Add seed URLs to frontier 
    printf("4. Adding seed URLs...\n");
    char *seeds[] = {
        "http://example.com",
        "http://example.org"
    };
    
    for (int i = 0; i < 2; i++) {
        int result = frontier_push(&frontier, seeds[i], 0);
        printf("   %s: %s\n", seeds[i], result ? "✓ Added" : "✗ Failed");
    }
    printf("\n");
    
    // 5. Start worker pool (Shiza's code) 
    printf("5. Starting worker pool with 2 threads...\n");
    WorkerPool pool;
    int result = worker_pool_start(&pool, &frontier, graph, 2);
    if (result != 0) {
        printf("ERROR: Failed to start worker pool\n");
        graph_destroy(graph);
        frontier_destroy(&frontier);
        return 1;
    }
    printf("   ✓ Worker pool started\n\n");
    
    // 6. Wait for workers to finish 
    printf("6. Crawling in progress...\n");
    printf("   (This may take a few seconds)\n\n");
    
    
    
    // Wait for 10 seconds or until frontier is empty 
    int wait_seconds = 0;
    while (wait_seconds < 10) {
        pthread_mutex_lock(&frontier.lock);
        int count = frontier.count;
        int discovered = frontier.total_discovered;
        pthread_mutex_unlock(&frontier.lock);
        
        if (count == 0 && discovered > 0) {
            printf("   Frontier empty after %d seconds\n", wait_seconds);
            break;
        }
        
        printf("   [%ds] Queue: %d, Discovered: %lu\n", 
               wait_seconds, count, discovered);
        sleep(1);
        wait_seconds++;
    }
    
    // Shutdown frontier 
    printf("\n7. Shutting down frontier...\n");
    frontier_shutdown(&frontier);
    
    // Wait for workers to finish 
    printf("   Waiting for workers to complete...\n");
    worker_pool_join(&pool);
    printf("   ✓ All workers finished\n\n");
    
    // 8. Get graph statistics 
    printf("8. Graph statistics:\n");
    uint64_t nodes, edges;
    graph_get_stats(graph, &nodes, &edges);
    printf("   Nodes: %lu\n", nodes);
    printf("   Edges: %lu\n\n", edges);
    
    // 9. Save graph for PageRank 
    printf("9. Saving graph for PageRank...\n");
    if (graph_save(graph, "crawled_graph.adj", "adjacency") == 0) {
        printf("   ✓ Saved crawled_graph.adj\n");
    }
    
    if (graph_save(graph, "crawled_graph.edge", "edgelist") == 0) {
        printf("   ✓ Saved crawled_graph.edge\n");
    }
    
    if (graph_save_url_map(graph, "url_map.txt") == 0) {
        printf("   ✓ Saved url_map.txt\n");
    }
    printf("\n");
    
    // 10. Print frontier statistics 
    printf("10. Frontier statistics:\n");
    printf("    Total discovered: %lu\n", frontier.total_discovered);
    printf("    Total fetched: %lu\n", frontier.total_fetched);
    printf("    Duplicates filtered: %lu\n", 
           frontier.total_discovered - frontier.total_fetched);
    printf("\n");
    
    // Cleanup
    printf("11. Cleaning up resources...\n");
    graph_destroy(graph);
    frontier_destroy(&frontier);
    
    if (policy->allowed_domains) free(policy->allowed_domains);
    if (policy->seed_domains) free(policy->seed_domains);
    pthread_mutex_destroy(&policy->lock);
    free(policy);
    printf("   ✓ Cleanup complete\n\n");
    
    printf("========================================\n");
    printf("  Milestone 1 Integration Test PASSED!\n");
    printf("========================================\n");
    
    return 0;
}