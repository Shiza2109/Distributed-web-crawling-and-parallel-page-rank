/*
 * m3_incremental.c — Milestone 3: Incremental Graph Growth Orchestrator
 *
 * Author : Aleena
 * Purpose: Extend the crawler to introduce new pages after initial
 *          PageRank convergence, demonstrating incremental graph updates.
 *
 * Pipeline:
 *   Phase 1  →  Initial crawl with seed URLs
 *            →  Save graph (phase1_graph.adj)
 *            →  Run PageRank until convergence (phase1_ranks.txt)
 *   Phase 2  →  Inject NEW seed URLs into the frontier
 *            →  Re-crawl (only new pages — duplicate filter preserved)
 *            →  Save expanded graph (phase2_graph.adj / phase2_graph.edge)
 *
 * The expanded graph is the input for the rest of Milestone 3:
 *   - Shiza:       incremental / partial PageRank recomputation
 *   - Abdurrehman: runtime instrumentation & recomputation trade-offs
 *   - Ibrahim:     structured evaluation (static vs incremental)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include "frontier.h"
#include "crawl_policy.h"
#include "worker.h"
#include "graph.h"
#include "fetch.h"
#include "parallel_pagerank.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static double now_ms(void)   { return (double)GetTickCount64(); }
#else
#include <unistd.h>
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
#endif

/* ============================================================
   Seed URL lists
   ============================================================ */

/* Phase 1 seeds — initial crawl (same domains used in M1/M2 testing) */
static const char *PHASE1_SEEDS[] = {
    "http://example.com",
    "http://example.org",
    "https://www.python.org/",
    "https://www.gnu.org/",
    "https://www.wikipedia.org/",
    "https://github.com/",
    "https://stackoverflow.com/",
    "https://www.bbc.com/"
};
static const int PHASE1_SEED_COUNT = sizeof(PHASE1_SEEDS) / sizeof(PHASE1_SEEDS[0]);

/*
 * Phase 2 seeds — NEW pages introduced after PageRank convergence.
 *
 * These are deliberately from different domains than Phase 1 so they
 * create genuinely new nodes and edges in the graph, demonstrating
 * incremental growth.  All are stable, well-linked pages.
 */
static const char *PHASE2_SEEDS[] = {
    "https://www.rust-lang.org/",     /* Systems programming — rich doc links   */
    "https://www.w3.org/",            /* Web standards body — many outlinks     */
    "https://www.nasa.gov/",          /* Government/science — stable, deep      */
    "https://nodejs.org/",            /* JS runtime — different ecosystem       */
    "https://www.docker.com/"         /* DevOps — bridges to cloud/infra pages  */
};
static const int PHASE2_SEED_COUNT = sizeof(PHASE2_SEEDS) / sizeof(PHASE2_SEEDS[0]);

/* ============================================================
   Helper: seed the frontier and return count of URLs accepted
   ============================================================ */

static int load_seeds(Frontier *fr, const char **seeds, int n) {
    int added = 0;
    for (int i = 0; i < n; i++) {
        if (frontier_push(fr, seeds[i], 0))
            added++;
    }
    return added;
}

/* ============================================================
   Helper: wait for threaded crawl to finish
   (same idle-detection logic as test_integration.c)
   ============================================================ */

static int wait_for_crawl(Frontier *frontier, WorkerPool *pool) {
    (void)pool;   /* workers self-exit on shutdown */

    int stable_idle = 0;
    while (stable_idle < 3) {
        uint64_t discovered, fetched;
        int count;

        pthread_mutex_lock(&frontier->lock);
        count      = frontier->count;
        discovered = frontier->total_discovered;
        fetched    = frontier->total_fetched;
        pthread_mutex_unlock(&frontier->lock);

        if (count == 0 && discovered > 0 && fetched >= discovered)
            stable_idle++;
        else
            stable_idle = 0;

        sleep_ms(200);
    }
    return 0;
}

/* ============================================================
   CLI parsing
   ============================================================ */

typedef struct {
    int      threads;
    int      max_depth;
    uint64_t max_pages;
} M3Config;

static void print_usage(const char *prog) {
    printf("Usage: %s [--threads N] [--depth N] [--max-pages N]\n", prog);
    printf("Defaults: --threads 4 --depth 0 --max-pages 100\n");
}

static int parse_args(int argc, char **argv, M3Config *cfg) {
    cfg->threads   = 4;
    cfg->max_depth = 0;
    cfg->max_pages = 100;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            cfg->threads = atoi(argv[++i]);
            if (cfg->threads <= 0 || cfg->threads > WORKER_MAX_THREADS) return -1;
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            cfg->max_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc) {
            cfg->max_pages = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

/* ============================================================
   MAIN — Milestone 3 Incremental Pipeline
   ============================================================ */

int main(int argc, char **argv) {
    M3Config cfg;
    if (parse_args(argc, argv, &cfg) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    printf("============================================================\n");
    printf("  Milestone 3 — Incremental Graph Growth (Aleena)\n");
    printf("============================================================\n");
    printf("  Threads   : %d\n",          cfg.threads);
    printf("  Max depth : %d\n",          cfg.max_depth);
    printf("  Max pages : %" PRIu64 "\n", cfg.max_pages);
    printf("============================================================\n\n");

    fetch_global_init();

    /* ── Shared resources ── */
    CrawlPolicy *policy = policy_create(cfg.max_depth, cfg.max_pages, 0);
    if (!policy) { fprintf(stderr, "Failed to create crawl policy\n"); return 1; }

    Frontier frontier;
    frontier_init(&frontier, policy);

    Graph *graph = graph_create();
    if (!graph) { fprintf(stderr, "Failed to create graph\n"); return 1; }

    WorkerPool pool;
    uint64_t nodes_before, edges_before;
    uint64_t nodes_after,  edges_after;

    /* ==========================================================
       PHASE 1 — Initial Crawl
       ========================================================== */
    printf("┌─────────────────────────────────────────────┐\n");
    printf("│  PHASE 1: Initial Crawl                     │\n");
    printf("└─────────────────────────────────────────────┘\n");

    policy_set_seed_domains(policy, (char **)PHASE1_SEEDS, PHASE1_SEED_COUNT);
    int p1_added = load_seeds(&frontier, PHASE1_SEEDS, PHASE1_SEED_COUNT);
    printf("[Phase 1] Seeds: %d requested, %d accepted\n",
           PHASE1_SEED_COUNT, p1_added);

    double p1_start = now_ms();

    if (worker_pool_start(&pool, &frontier, graph, cfg.threads) != 0) {
        fprintf(stderr, "Failed to start worker pool\n");
        return 1;
    }
    wait_for_crawl(&frontier, &pool);
    frontier_shutdown(&frontier);
    worker_pool_join(&pool);

    double p1_elapsed = now_ms() - p1_start;

    graph_get_stats(graph, &nodes_before, &edges_before);
    printf("[Phase 1] Crawl complete: %" PRIu64 " nodes, %" PRIu64 " edges (%.0f ms)\n",
           nodes_before, edges_before, p1_elapsed);

    /* Save Phase 1 graph */
    graph_save(graph, "phase1_graph.adj", "adjacency");
    graph_save(graph, "phase1_graph.edge", "edgelist");
    graph_save_url_map(graph, "phase1_url_map.txt");

    /* ==========================================================
       PHASE 1 — PageRank (convergence-based)
       ========================================================== */
    printf("\n┌─────────────────────────────────────────────┐\n");
    printf("│  PHASE 1: PageRank Convergence              │\n");
    printf("└─────────────────────────────────────────────┘\n");

    ParallelGraph *pg = parallel_load_graph("phase1_graph.adj");
    if (!pg) {
        fprintf(stderr, "Failed to load Phase 1 graph for PageRank\n");
        return 1;
    }

    printf("[PageRank] Running parallel PageRank on Phase 1 graph "
           "(%d nodes, %d threads)...\n", pg->num_nodes, cfg.threads);

    parallel_pagerank_compute(pg, cfg.threads);
    parallel_pagerank_save_ranks(pg, "phase1_ranks.txt");
    parallel_pagerank_print_ranks(pg);
    printf("[PageRank] Phase 1 ranks saved to phase1_ranks.txt\n");

    parallel_free_graph(pg);

    /* ==========================================================
       PHASE 2 — Inject New Pages & Re-Crawl
       ========================================================== */
    printf("\n┌─────────────────────────────────────────────┐\n");
    printf("│  PHASE 2: Incremental Crawl (New Pages)     │\n");
    printf("└─────────────────────────────────────────────┘\n");

    /* Reset the frontier so workers can pop again.
       The seen_urls set stays intact — already-crawled pages will
       be skipped by the duplicate filter. */
    frontier_reset_for_recrawl(&frontier);

    /* Reset policy counters so we get a fresh budget for Phase 2 */
    pthread_mutex_lock(&policy->lock);
    policy->pages_fetched = 0;
    policy->should_stop   = 0;
    pthread_mutex_unlock(&policy->lock);

    /* Add the new seed domains to the policy's allowed list */
    policy_set_seed_domains(policy, (char **)PHASE2_SEEDS, PHASE2_SEED_COUNT);

    printf("[Phase 2] Introducing new seed URLs:\n");
    int p2_added = 0;
    for (int i = 0; i < PHASE2_SEED_COUNT; i++) {
        int ok = frontier_push(&frontier, PHASE2_SEEDS[i], 0);
        if (ok) {
            printf("  [+] %s\n", PHASE2_SEEDS[i]);
            p2_added++;
        } else {
            printf("  [skip] %s (already seen or filtered)\n", PHASE2_SEEDS[i]);
        }
    }
    printf("[Phase 2] New seeds: %d/%d accepted\n",
           p2_added, PHASE2_SEED_COUNT);

    double p2_start = now_ms();

    if (worker_pool_start(&pool, &frontier, graph, cfg.threads) != 0) {
        fprintf(stderr, "Failed to start worker pool for Phase 2\n");
        return 1;
    }
    wait_for_crawl(&frontier, &pool);
    frontier_shutdown(&frontier);
    worker_pool_join(&pool);

    double p2_elapsed = now_ms() - p2_start;

    graph_get_stats(graph, &nodes_after, &edges_after);
    printf("[Phase 2] Crawl complete: %" PRIu64 " nodes, %" PRIu64 " edges (%.0f ms)\n",
           nodes_after, edges_after, p2_elapsed);

    /* Save Phase 2 (expanded) graph */
    graph_save(graph, "phase2_graph.adj", "adjacency");
    graph_save(graph, "phase2_graph.edge", "edgelist");
    graph_save_url_map(graph, "phase2_url_map.txt");

    /* ==========================================================
       SUMMARY
       ========================================================== */
    printf("\n============================================================\n");
    printf("  INCREMENTAL GROWTH SUMMARY\n");
    printf("============================================================\n");
    printf("  Phase 1 (initial)     : %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_before, edges_before);
    printf("  Phase 2 (after inject): %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_after, edges_after);
    printf("  New nodes added       : %" PRIu64 "\n",
           nodes_after - nodes_before);
    printf("  New edges added       : %" PRIu64 "\n",
           edges_after - edges_before);
    printf("  Phase 1 crawl time    : %.0f ms\n", p1_elapsed);
    printf("  Phase 2 crawl time    : %.0f ms\n", p2_elapsed);
    printf("============================================================\n");
    printf("\n  Output files:\n");
    printf("    phase1_graph.adj / .edge   — graph before new pages\n");
    printf("    phase1_ranks.txt           — converged PageRank scores\n");
    printf("    phase1_url_map.txt         — URL-to-ID mapping (phase 1)\n");
    printf("    phase2_graph.adj / .edge   — graph after new pages\n");
    printf("    phase2_url_map.txt         — URL-to-ID mapping (phase 2)\n");
    printf("\n  Next steps (other M3 members):\n");
    printf("    Shiza:       run incremental PageRank on phase2_graph.adj\n");
    printf("    Abdurrehman: instrument and compare recomputation cost\n");
    printf("    Ibrahim:     evaluate static vs incremental performance\n");
    printf("============================================================\n");

    /* ── Cleanup ── */
    graph_destroy(graph);
    frontier_destroy(&frontier);
    policy_destroy(policy);
    fetch_global_cleanup();

    return 0;
}
