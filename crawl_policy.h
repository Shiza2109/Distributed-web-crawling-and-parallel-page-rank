/* crawl_policy.h - Add to frontier module */
#ifndef CRAWL_POLICY_H
#define CRAWL_POLICY_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct {
    /* Configuration */
    int max_depth;              /* Maximum crawl depth (0 = seeds only) */
    uint64_t max_pages;         /* Maximum pages to crawl */
    char **allowed_domains;     /* NULL = no domain restriction */
    int num_allowed_domains;
    int same_domain_only;       /* 1 = only crawl same domain as seeds */
    
    /* Seed domains (extracted from seed URLs) */
    char **seed_domains;
    int num_seed_domains;
    
    /* Runtime state */
    uint64_t pages_fetched;     /* Total pages fetched so far */
    int should_stop;            /* Flag to stop crawling */
    
    /* Thread safety */
    pthread_mutex_t lock;
} CrawlPolicy;

/* Initialize crawl policy */
CrawlPolicy* policy_create(int max_depth, uint64_t max_pages, int same_domain_only);

/* Set allowed domains from seed URLs */
void policy_set_seed_domains(CrawlPolicy *policy, char **seed_urls, int num_seeds);

/* Check if URL is within scope */
int policy_is_url_allowed(CrawlPolicy *policy, const char *url, int current_depth);

/* Check if crawl should continue (not hit limits) */
int policy_can_continue(CrawlPolicy *policy);

/* Record that a page was fetched */
void policy_record_fetch(CrawlPolicy *policy);

/* Extract domain from URL (helper) */
void policy_extract_domain(const char *url, char *domain_out, size_t max_len);

#endif