/* fetch.h */
#ifndef FETCH_H
#define FETCH_H

#include "frontier.h"   /* for FetchOutcome, FR_MAX_URL_LEN */

#define FETCH_MAX_RESPONSE_BYTES  (5 * 1024 * 1024)
#define FETCH_CONNECT_TIMEOUT_SEC  5
#define FETCH_TOTAL_TIMEOUT_SEC   15

typedef struct {
    char          url[FR_MAX_URL_LEN];      /* final URL after redirects     */
    char         *html;                     /* heap-allocated, NULL on skip  */
    size_t        html_len;
    int           http_code;
    FetchOutcome  outcome;
} FetchResult;

/*
 * fetch_url — perform one HTTP GET.
 * Fills result. Caller must call fetch_result_free() when done.
 * Thread safe (each call creates its own CURL handle).
 */
void fetch_url(const char *url, const char *user_agent, FetchResult *result);

/*
 * fetch_result_free — release html buffer inside result.
 */
void fetch_result_free(FetchResult *result);

/*
 * fetch_global_init / fetch_global_cleanup — call once from main(),
 * before and after all threads.
 */
void fetch_global_init(void);
void fetch_global_cleanup(void);

#endif