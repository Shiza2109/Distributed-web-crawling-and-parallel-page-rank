#include "manual_worker.h"

#include <string.h>

#include "fetch.h"
#include "parser.h"

static int is_queue_empty(Frontier *fr) {
    int empty;
    pthread_mutex_lock(&fr->lock);
    empty = (fr->count == 0);
    pthread_mutex_unlock(&fr->lock);
    return empty;
}

static void process_one_url(Frontier *frontier, Graph *graph, const char *url, int depth) {
    FetchResult result;
    char links[PARSER_MAX_LINKS_PER_PAGE][FR_MAX_URL_LEN];
    int link_count;
    const char *user_agent = "Web-Crawler";

    fetch_url(url, user_agent, &result);

    if (result.outcome == FETCH_REDIRECT) {
        frontier_mark_fetched(frontier, url, FETCH_REDIRECT,
                              result.url[0] ? result.url : NULL,
                              result.http_code, depth);
        fetch_result_free(&result);
        return;
    }

    if (result.outcome != FETCH_OK) {
        frontier_mark_fetched(frontier, url, result.outcome, NULL,
                              result.http_code, depth);
        fetch_result_free(&result);
        return;
    }

    link_count = parser_extract_links(result.html, result.url,
                                      links, PARSER_MAX_LINKS_PER_PAGE);
    if (link_count > 0) {
        frontier_push_links(frontier, links, link_count, result.url, depth + 1);
    }

    for (int i = 0; i < link_count; i++) {
        graph_add_edge(graph, result.url, links[i]);
    }

    frontier_mark_fetched(frontier, url, FETCH_OK, NULL, result.http_code, depth);
    fetch_result_free(&result);
}

int run_manual_crawl(Frontier *frontier, Graph *graph) {
    char url[FR_MAX_URL_LEN];
    int depth;

    while (!is_queue_empty(frontier)) {
        if (!frontier_pop(frontier, url, &depth)) {
            break;
        }
        process_one_url(frontier, graph, url, depth);
    }
    return 0;
}
