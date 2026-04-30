#include "frontier.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
   Hash Set Implementation for Duplicate URL Filtering
   ============================================================ */

/* Simple but effective hash function (djb2) */
static unsigned long hash_url(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + tolower(c); /* Case-insensitive */
    }
    return hash;
}

/* Initialize hash set */
static int hash_set_init(HashSet *set, size_t initial_size) {
    set->buckets = calloc(initial_size, sizeof(HashSetEntry*));
    if (!set->buckets) return -1;
    set->size = initial_size;
    set->count = 0;
    pthread_mutex_init(&set->lock, NULL);
    return 0;
}

/* Check if URL exists in hash set (case-insensitive) */
static int hash_set_contains(HashSet *set, const char *url) {
    unsigned long hash = hash_url(url);
    size_t index = hash % set->size;
    
    HashSetEntry *entry = set->buckets[index];
    while (entry) {
        if (strcasecmp(entry->url, url) == 0) {
            return 1; /* Found */
        }
        entry = entry->next;
    }
    return 0; /* Not found */
}

/* Add URL to hash set if not already present */
static int hash_set_add(HashSet *set, const char *url) {
    pthread_mutex_lock(&set->lock);
    
    /* Check if already exists */
    if (hash_set_contains(set, url)) {
        pthread_mutex_unlock(&set->lock);
        return 0; /* Already exists */
    }
    
    /* Add new entry */
    unsigned long hash = hash_url(url);
    size_t index = hash % set->size;
    
    HashSetEntry *entry = malloc(sizeof(HashSetEntry));
    if (!entry) {
        pthread_mutex_unlock(&set->lock);
        return -1; /* Error */
    }
    
    strncpy(entry->url, url, FR_MAX_URL_LEN - 1);
    entry->url[FR_MAX_URL_LEN - 1] = '\0';
    entry->next = set->buckets[index];
    set->buckets[index] = entry;
    set->count++;
    
    /* Resize if load factor > 0.75 */
    if ((double)set->count / set->size > 0.75) {
        size_t new_size = set->size * 2;
        HashSetEntry **new_buckets = calloc(new_size, sizeof(HashSetEntry*));
        
        if (new_buckets) {
            /* Rehash all entries */
            for (size_t i = 0; i < set->size; i++) {
                HashSetEntry *entry = set->buckets[i];
                while (entry) {
                    HashSetEntry *next = entry->next;
                    unsigned long new_hash = hash_url(entry->url);
                    size_t new_index = new_hash % new_size;
                    entry->next = new_buckets[new_index];
                    new_buckets[new_index] = entry;
                    entry = next;
                }
            }
            free(set->buckets);
            set->buckets = new_buckets;
            set->size = new_size;
        }
    }
    
    pthread_mutex_unlock(&set->lock);
    return 1; /* Added new */
}

/* Free hash set */
static void hash_set_destroy(HashSet *set) {
    if (!set->buckets) return;
    
    for (size_t i = 0; i < set->size; i++) {
        HashSetEntry *entry = set->buckets[i];
        while (entry) {
            HashSetEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(set->buckets);
    pthread_mutex_destroy(&set->lock);
}

/* ============================================================
   URL Table (for tracking URL states)
   ============================================================ */

static int find_url_in_table(Frontier *fr, const char *url) {
    for (int i = 0; i < fr->url_table.used; i++) {
        if (strcasecmp(fr->url_table.urls[i], url) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_url_to_table(Frontier *fr, const char *url, UrlState state) {
    if (fr->url_table.used >= fr->url_table.size) {
        int new_size = fr->url_table.size * 2;
        if (new_size == 0) new_size = 10000;
        
        char **new_urls = realloc(fr->url_table.urls, new_size * sizeof(char*));
        UrlState *new_states = realloc(fr->url_table.states, new_size * sizeof(UrlState));
        
        if (!new_urls || !new_states) return;
        
        fr->url_table.urls = new_urls;
        fr->url_table.states = new_states;
        fr->url_table.size = new_size;
    }
    
    fr->url_table.urls[fr->url_table.used] = strdup(url);
    fr->url_table.states[fr->url_table.used] = state;
    fr->url_table.used++;
}

/* ============================================================
   Frontier Public API
   ============================================================ */

void frontier_init(Frontier *fr, CrawlPolicy *policy) {
    /* Initialize queue */
    fr->queue = malloc(FRONTIER_INITIAL_CAPACITY * sizeof(FrontierEntry));
    fr->head = 0;
    fr->tail = 0;
    fr->count = 0;
    fr->capacity = FRONTIER_INITIAL_CAPACITY;
    fr->shutdown_flag = 0;
    fr->total_discovered = 0;
    fr->total_fetched = 0;
    fr->policy = policy;
    
    /* Initialize duplicate filtering hash set */
    hash_set_init(&fr->seen_urls, 65536); /* 64K initial buckets */
    
    /* Initialize URL table */
    fr->url_table.urls = NULL;
    fr->url_table.states = NULL;
    fr->url_table.size = 0;
    fr->url_table.used = 0;
    
    /* Initialize synchronization */
    pthread_mutex_init(&fr->lock, NULL);
    pthread_cond_init(&fr->not_empty, NULL);
    pthread_mutex_init(&fr->url_table.lock, NULL);
}

/* Check if URL is a duplicate */
int frontier_is_duplicate(Frontier *fr, const char *url) {
    if (!fr || !url) return 1;
    
    /* Check in hash set (fast O(1) lookup) */
    return hash_set_contains(&fr->seen_urls, url);
}

/* Mark URL as seen (add to duplicate filter) */
void frontier_mark_seen(Frontier *fr, const char *url) {
    if (!fr || !url) return;
    hash_set_add(&fr->seen_urls, url);
}

/* Push a single URL to frontier with duplicate filtering */
int frontier_push(Frontier *fr, const char *url, int depth) {
    if (!fr || !url) return 0;
    
    /* Check depth limit */
    if (fr->policy && depth > fr->policy->max_depth) {
        return 0;
    }
    
    /* Check page limit */
    if (fr->policy && fr->policy->max_pages > 0 && 
        fr->total_discovered >= fr->policy->max_pages) {
        return 0;
    }
    
    /* DUPLICATE FILTERING: Check if URL already seen */
    if (frontier_is_duplicate(fr, url)) {
        return 0;
    }
    
    pthread_mutex_lock(&fr->lock);
    
    /* Resize queue if needed */
    if (fr->count >= fr->capacity) {
        int new_capacity = fr->capacity * 2;
        FrontierEntry *new_queue = realloc(fr->queue, new_capacity * sizeof(FrontierEntry));
        if (!new_queue) {
            pthread_mutex_unlock(&fr->lock);
            return 0;
        }
        
        /* Reorganize queue if it's wrapped around */
        if (fr->tail < fr->head) {
            memmove(new_queue + fr->capacity, new_queue, fr->tail * sizeof(FrontierEntry));
            fr->tail += fr->capacity;
        }
        
        fr->queue = new_queue;
        fr->capacity = new_capacity;
    }
    
    /* Add to queue */
    strncpy(fr->queue[fr->tail].url, url, FR_MAX_URL_LEN - 1);
    fr->queue[fr->tail].url[FR_MAX_URL_LEN - 1] = '\0';
    fr->queue[fr->tail].depth = depth;
    fr->tail = (fr->tail + 1) % fr->capacity;
    fr->count++;
    fr->total_discovered++;
    
    /* Mark as seen in duplicate filter */
    frontier_mark_seen(fr, url);
    
    /* Track state in URL table */
    pthread_mutex_lock(&fr->url_table.lock);
    int idx = find_url_in_table(fr, url);
    if (idx == -1) {
        add_url_to_table(fr, url, URL_QUEUED);
    }
    pthread_mutex_unlock(&fr->url_table.lock);
    
    pthread_cond_signal(&fr->not_empty);
    pthread_mutex_unlock(&fr->lock);
    
    return 1;
}

/* Push multiple links with duplicate filtering */
int frontier_push_links(Frontier *fr, char urls[][FR_MAX_URL_LEN], 
                        int count, const char *parent_url, int depth) {
    int added = 0;
    
    for (int i = 0; i < count; i++) {
        if (frontier_push(fr, urls[i], depth)) {
            added++;
        }
    }
    
    return added;
}

/* Pop a URL from frontier */
int frontier_pop(Frontier *fr, char *url, int *depth) {
    pthread_mutex_lock(&fr->lock);
    
    while (fr->count == 0 && !fr->shutdown_flag) {
        pthread_cond_wait(&fr->not_empty, &fr->lock);
    }
    
    if (fr->shutdown_flag && fr->count == 0) {
        pthread_mutex_unlock(&fr->lock);
        return 0;
    }
    
    /* Pop from queue */
    strcpy(url, fr->queue[fr->head].url);
    *depth = fr->queue[fr->head].depth;
    fr->head = (fr->head + 1) % fr->capacity;
    fr->count--;
    
    /* Update state */
    pthread_mutex_lock(&fr->url_table.lock);
    int idx = find_url_in_table(fr, url);
    if (idx >= 0) {
        fr->url_table.states[idx] = URL_FETCHING;
    }
    pthread_mutex_unlock(&fr->url_table.lock);
    
    pthread_mutex_unlock(&fr->lock);
    return 1;
}

/* Mark URL as fetched */
void frontier_mark_fetched(Frontier *fr, const char *url, 
                           FetchOutcome outcome, 
                           const char *redirect_url, 
                           int http_code,
                           int current_depth) {
    pthread_mutex_lock(&fr->url_table.lock);
    int idx = find_url_in_table(fr, url);
    if (idx >= 0) {
        if (outcome == FETCH_OK || outcome == FETCH_REDIRECT) {
            fr->url_table.states[idx] = URL_DONE;
        } else {
            fr->url_table.states[idx] = URL_FAILED;
        }
    }
    pthread_mutex_unlock(&fr->url_table.lock);
    
    fr->total_fetched++;
    
    /* Handle redirect */
    if (outcome == FETCH_REDIRECT && redirect_url && redirect_url[0]) {
        /* Keep redirects at the same crawl depth as the source URL. */
        frontier_push(fr, redirect_url, current_depth);
    }
}

/* Shutdown frontier */
void frontier_shutdown(Frontier *fr) {
    pthread_mutex_lock(&fr->lock);
    fr->shutdown_flag = 1;
    pthread_cond_broadcast(&fr->not_empty);
    pthread_mutex_unlock(&fr->lock);
}

/* Destroy frontier and free resources */
void frontier_destroy(Frontier *fr) {
    /* Free URL table */
    for (int i = 0; i < fr->url_table.used; i++) {
        free(fr->url_table.urls[i]);
    }
    free(fr->url_table.urls);
    free(fr->url_table.states);
    
    /* Free hash set */
    hash_set_destroy(&fr->seen_urls);
    
    /* Free queue */
    free(fr->queue);
    
    /* Destroy synchronization */
    pthread_mutex_destroy(&fr->lock);
    pthread_cond_destroy(&fr->not_empty);
    pthread_mutex_destroy(&fr->url_table.lock);
}