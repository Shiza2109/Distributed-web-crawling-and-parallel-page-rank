#include <stdio.h>
#include <stdlib.h>
#include "frontier.h"
#include "crawl_policy.h"

int main() {
    CrawlPolicy *policy = policy_create(3, 1000, 0);
    Frontier frontier;
    frontier_init(&frontier, policy);
    
    /* Test duplicate filtering */
    printf("\n=== Testing Duplicate URL Filtering ===\n\n");
    
    printf("Adding: http://example.com\n");
    int r1 = frontier_push(&frontier, "http://example.com", 0);
    printf("Result: %s\n\n", r1 ? "✓ Added" : "✗ Failed");
    
    printf("Adding: http://EXAMPLE.com (should be filtered)\n");
    int r2 = frontier_push(&frontier, "http://EXAMPLE.com", 0);
    printf("Result: %s\n\n", r2 ? "✗ Added (ERROR)" : "✓ Filtered");
    
    printf("Adding: http://example.com/page\n");
    int r3 = frontier_push(&frontier, "http://example.com/page", 0);
    printf("Result: %s\n\n", r3 ? "✓ Added" : "✗ Failed");
    
    printf("Adding: http://example.com/page (duplicate, should be filtered)\n");
    int r4 = frontier_push(&frontier, "http://example.com/page", 0);
    printf("Result: %s\n\n", r4 ? "✗ Added (ERROR)" : "✓ Filtered");
    
    printf("=== Results ===\n");
    printf("Expected: 2 unique URLs queued\n");
    printf("Actual: %d in queue\n\n", frontier.count);
    
    /* SHUTDOWN THE FRONTIER BEFORE POPPING */
    frontier_shutdown(&frontier);
    
    /* Display URLs in queue */
    printf("URLs in queue:\n");
    char url[FR_MAX_URL_LEN];
    int depth;
    int count = 0;
    while (frontier_pop(&frontier, url, &depth)) {
        printf("  %d. %s (depth: %d)\n", ++count, url, depth);
    }
    
    frontier_destroy(&frontier);
    
    /* Free policy */
    if (policy) {
        if (policy->allowed_domains) free(policy->allowed_domains);
        if (policy->seed_domains) free(policy->seed_domains);
        pthread_mutex_destroy(&policy->lock);
        free(policy);
    }
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
