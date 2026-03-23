/* reproducibility.h */
#ifndef REPRODUCIBILITY_H
#define REPRODUCIBILITY_H

#include "crawl_policy.h"
#include <stdint.h>

typedef struct {
    /* Crawl configuration */
    char **seed_urls;
    int num_seeds;
    int max_depth;
    uint64_t max_pages;
    int same_domain_only;
    int num_workers;
    
    /* Results */
    uint64_t total_nodes;
    uint64_t total_edges;
    uint64_t pages_fetched;
    
    /* Timestamp and hash */
    char timestamp[64];
    uint64_t content_hash;  /* For verifying reproducibility */
    
    /* Output files */
    char graph_file[256];
    char url_map_file[256];
    char log_file[256];
} CrawlManifest;

/* Save complete crawl manifest */
void manifest_save(CrawlManifest *m, const char *filename);

/* Compute hash of all URLs (for reproducibility verification) */
uint64_t manifest_compute_hash(const char **urls, int count);

#endif