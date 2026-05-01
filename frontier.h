#ifndef FRONTIER_H
#define FRONTIER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "crawl_policy.h"

#define FR_MAX_URL_LEN 4096
#define FRONTIER_INITIAL_CAPACITY 10000

typedef enum {
    FETCH_OK,
    FETCH_REDIRECT,
    FETCH_TIMEOUT,
    FETCH_NOT_FOUND,
    FETCH_FORBIDDEN,
    FETCH_GONE,
    FETCH_SERVER_ERROR,
    FETCH_CONTENT_SKIP,
    FETCH_ERROR
} FetchOutcome;

/* URL states for duplicate tracking */
typedef enum {
    URL_UNSEEN,      /* Not encountered yet */
    URL_QUEUED,      /* In queue waiting to be fetched */
    URL_FETCHING,    /* Currently being fetched by a worker */
    URL_DONE,        /* Successfully fetched */
    URL_FAILED       /* Failed to fetch */
} UrlState;

/* Hash set entry for exact URL tracking */
typedef struct HashSetEntry {
    char url[FR_MAX_URL_LEN];
    struct HashSetEntry *next;
} HashSetEntry;

/* Hash set for duplicate detection */
typedef struct {
    HashSetEntry **buckets;
    size_t size;
    size_t count;
    pthread_mutex_t lock;
} HashSet;

/* Frontier queue entry */
typedef struct {
    char url[FR_MAX_URL_LEN];
    int depth;
} FrontierEntry;

/* Main Frontier structure */
typedef struct {
    /* Queue */
    FrontierEntry *queue;
    int head;
    int tail;
    int count;
    int capacity;
    
    /* URL tracking for duplicate filtering */
    HashSet seen_urls;           /* All URLs ever seen */
    
    /* URL states for tracking progress */
    struct {
        char **urls;
        UrlState *states;
        int size;
        int used;
        pthread_mutex_t lock;
    } url_table;
    
    /* Synchronization */
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    int shutdown_flag;
    
    /* Statistics */
    uint64_t total_discovered;
    uint64_t total_fetched;
    
    /* Crawl policy */
    CrawlPolicy *policy;
    
} Frontier;

/* Frontier API */
void frontier_init(Frontier *fr, CrawlPolicy *policy);
int frontier_push(Frontier *fr, const char *url, int depth);
int frontier_push_links(Frontier *fr, char urls[][FR_MAX_URL_LEN], 
                        int count, const char *parent_url, int depth);
int frontier_pop(Frontier *fr, char *url, int *depth);
void frontier_mark_fetched(Frontier *fr, const char *url, 
                           FetchOutcome outcome, 
                           const char *redirect_url, 
                           int http_code,
                           int current_depth);
void frontier_shutdown(Frontier *fr);
void frontier_destroy(Frontier *fr);

/* Duplicate filtering functions */
int frontier_is_duplicate(Frontier *fr, const char *url);
void frontier_mark_seen(Frontier *fr, const char *url);

/*
 * frontier_reset_for_recrawl — clear the shutdown flag so workers
 * can pop new URLs again after an initial crawl completes.
 * The seen_urls hash set is kept intact so already-crawled pages
 * are NOT re-fetched.  (Aleena — Milestone 3)
 */
void frontier_reset_for_recrawl(Frontier *fr);

#endif