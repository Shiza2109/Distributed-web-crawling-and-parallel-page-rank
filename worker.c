/* worker.c */

#include "worker.h"
#include "fetch.h"
#include "parser.h"
#include "graph.h"
#include "frontier.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── link buffer: stack-allocated inside each worker thread ── */
typedef struct {
    char urls[PARSER_MAX_LINKS_PER_PAGE][FR_MAX_URL_LEN];
    int  count;
} LinkBuf;

/* ═══════════════════════════════════════════════════
   worker_thread — the main loop of one crawler worker
   ═══════════════════════════════════════════════════ */
static void *worker_thread(void *arg) {
    WorkerArgs *w = arg;

    char       url[FR_MAX_URL_LEN];
    int        depth;
    FetchResult result;
    LinkBuf    links;

    printf("[Worker %d] started\n", w->worker_id);

    while (frontier_pop(w->frontier, url, &depth)) {

        printf("[Worker %d] depth=%d  %s\n", w->worker_id, depth, url);

        /* ── 1. fetch ── */
        fetch_url(url, w->frontier->policy.user_agent, &result);

        /* ── 2. handle redirect ──
               frontier_mark_fetched re-queues the Location URL          */
        if (result.outcome == FETCH_REDIRECT) {
            /* result.url holds the Location value curl captured          */
            frontier_mark_fetched(w->frontier, url,
                                  FETCH_REDIRECT,
                                  result.url[0] ? result.url : NULL,
                                  result.http_code);
            continue;
        }

        /* ── 3. skip / error — report and move on ── */
        if (result.outcome != FETCH_OK) {
            fprintf(stderr, "[Worker %d] %s  http=%d  url=%s\n",
                    w->worker_id,
                    result.outcome == FETCH_TIMEOUT      ? "TIMEOUT"  :
                    result.outcome == FETCH_NOT_FOUND    ? "404"      :
                    result.outcome == FETCH_FORBIDDEN    ? "403"      :
                    result.outcome == FETCH_GONE         ? "410"      :
                    result.outcome == FETCH_SERVER_ERROR ? "5xx"      :
                    result.outcome == FETCH_CONTENT_SKIP ? "NON-HTML" : "ERROR",
                    result.http_code, url);
            frontier_mark_fetched(w->frontier, url,
                                  result.outcome, NULL, result.http_code);
            continue;
        }

        /* ── 4. extract links (teammate's parser) ── */
        links.count = parser_extract_links(result.html, result.url,
                                            links.urls,
                                            PARSER_MAX_LINKS_PER_PAGE);

        /* ── 5. push discovered links back into frontier ── */
        if (links.count > 0)
            frontier_push_links(w->frontier,
                                 links.urls,
                                 links.count,
                                 result.url,      /* parent URL for relative resolution */
                                 depth + 1);

        /* ── 6. record graph edges (teammate's graph module) ── */
        for (int i = 0; i < links.count; i++)
            graph_add_edge(w->graph, result.url, links.urls[i]);

        /* ── 7. tell frontier this URL succeeded ── */
        frontier_mark_fetched(w->frontier, url,
                               FETCH_OK, NULL, result.http_code);

        /* ── 8. free HTML buffer ── */
        fetch_result_free(&result);
    }

    printf("[Worker %d] done\n", w->worker_id);
    return NULL;
}

/* ═══════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════ */

int worker_pool_start(WorkerPool *pool,
                       Frontier   *frontier,
                       Graph      *graph,
                       int         n_threads) {
    if (n_threads <= 0 || n_threads > WORKER_MAX_THREADS)
        return -1;

    pool->n_threads = n_threads;

    for (int i = 0; i < n_threads; i++) {
        pool->args[i].worker_id = i;
        pool->args[i].frontier  = frontier;
        pool->args[i].graph     = graph;

        int rc = pthread_create(&pool->threads[i], NULL,
                                 worker_thread, &pool->args[i]);
        if (rc != 0) {
            fprintf(stderr, "[pool] pthread_create failed for worker %d: %d\n",
                    i, rc);
            /* signal already-running workers to stop */
            frontier_shutdown(frontier);
            /* join the ones that started */
            for (int j = 0; j < i; j++)
                pthread_join(pool->threads[j], NULL);
            return -1;
        }
    }

    printf("[Pool] %d workers started\n", n_threads);
    return 0;
}

void worker_pool_join(WorkerPool *pool) {
    for (int i = 0; i < pool->n_threads; i++)
        pthread_join(pool->threads[i], NULL);
    printf("[Pool] all workers joined\n");
}
```

---

## How the two files relate to everything else
```
frontier_pop()          ← blocks worker until a URL is ready
      ↓
fetch_url()             ← fetch.c: libcurl GET, content-type check
      ↓
parser_extract_links()  ← teammate's parser.c: extract <a href>
      ↓
frontier_push_links()   ← feeds new URLs back into the frontier
graph_add_edge()        ← teammate's graph.c: writes edge to TSV
      ↓
frontier_mark_fetched() ← tells frontier: success / retry / discard
fetch_result_free()     ← free HTML buffer