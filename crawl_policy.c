#include "crawl_policy.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

CrawlPolicy* policy_create(int max_depth, uint64_t max_pages, int same_domain_only) {
    CrawlPolicy *policy = malloc(sizeof(CrawlPolicy));
    if (!policy) return NULL;
    
    policy->max_depth = max_depth;
    policy->max_pages = max_pages;
    policy->same_domain_only = same_domain_only;
    policy->pages_fetched = 0;
    policy->should_stop = 0;
    policy->allowed_domains = NULL;
    policy->num_allowed_domains = 0;
    policy->seed_domains = NULL;
    policy->num_seed_domains = 0;
    
    pthread_mutex_init(&policy->lock, NULL);
    
    return policy;
}

void policy_set_seed_domains(CrawlPolicy *policy, char **seed_urls, int num_seeds) {
    if (!policy) return;
    
    /* Free existing */
    if (policy->seed_domains) {
        for (int i = 0; i < policy->num_seed_domains; i++) {
            free(policy->seed_domains[i]);
        }
        free(policy->seed_domains);
    }
    
    /* Allocate and copy new seeds */
    policy->seed_domains = malloc(num_seeds * sizeof(char*));
    if (!policy->seed_domains) return;
    
    for (int i = 0; i < num_seeds; i++) {
        policy->seed_domains[i] = strdup(seed_urls[i]);
    }
    policy->num_seed_domains = num_seeds;
}

int policy_is_url_allowed(CrawlPolicy *policy, const char *url, int current_depth) {
    if (!policy) return 1;
    
    /* Check depth limit */
    if (current_depth > policy->max_depth) {
        return 0;
    }
    
    /* Check if we have seed domains and same_domain_only is set */
    if (policy->same_domain_only && policy->num_seed_domains > 0) {
        char domain[256];
        policy_extract_domain(url, domain, sizeof(domain));
        
        /* Check if domain matches any seed domain */
        for (int i = 0; i < policy->num_seed_domains; i++) {
            if (strcasecmp(domain, policy->seed_domains[i]) == 0) {
                return 1;
            }
        }
        return 0; /* Domain not in allowed list */
    }
    
    return 1;
}

int policy_can_continue(CrawlPolicy *policy) {
    if (!policy) return 1;
    return (policy->pages_fetched < policy->max_pages && !policy->should_stop);
}

void policy_record_fetch(CrawlPolicy *policy) {
    if (policy) {
        policy->pages_fetched++;
    }
}

void policy_extract_domain(const char *url, char *domain_out, size_t max_len) {
    /* Find the protocol separator */
    const char *start = strstr(url, "://");
    if (start) {
        start += 3;  /* Skip "://" */
    } else {
        start = url;
    }
    
    /* Find the end of domain (slash or end of string) */
    const char *end = strchr(start, '/');
    if (!end) {
        end = start + strlen(start);
    }
    
    /* Copy domain */
    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    strncpy(domain_out, start, len);
    domain_out[len] = '\0';
}
