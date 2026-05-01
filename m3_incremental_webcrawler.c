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
/* UNUSED — Phase 1 now uses pre-generated graphs
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
*/

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
   UNUSED — Phase 1 now uses pre-generated graphs
   ============================================================ */
/*
static int load_seeds(Frontier *fr, const char **seeds, int n) {
    int added = 0;
    for (int i = 0; i < n; i++) {
        if (frontier_push(fr, seeds[i], 0))
            added++;
    }
    return added;
}
*/

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
   PHASE 2 HELPERS: File operations and ID-offset merge
   ============================================================ */

/* Copy file efficiently */
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int append_offset_adjacency(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "a");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[65536];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        long long src_id = atoll(line);
        fprintf(out, "%" PRIu64 ":", (uint64_t)src_id + offset);

        char *neighbors = colon + 1;
        char *token = strtok(neighbors, ",\r\n");
        int first = 1;
        while (token) {
            long long dst_id = atoll(token);
            if (!first) fprintf(out, ",");
            fprintf(out, "%" PRIu64, (uint64_t)dst_id + offset);
            first = 0;
            token = strtok(NULL, ",\r\n");
        }
        fprintf(out, "\n");
    }

    fclose(in);
    fclose(out);
    return 0;
}

static int append_offset_edges(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "a");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        long long src_id, dst_id;
        if (sscanf(line, "%lld %lld", &src_id, &dst_id) == 2) {
            fprintf(out, "%" PRIu64 " %" PRIu64 "\n",
                    (uint64_t)src_id + offset, (uint64_t)dst_id + offset);
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

static int append_offset_url_map(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "a");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[8192];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        long long node_id;
        char url[7000];
        if (sscanf(line, "%lld: %6999s", &node_id, url) == 2) {
            fprintf(out, "%" PRIu64 ": %s\n", (uint64_t)node_id + offset, url);
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Write offsetted adjacency file (creates dst) */
static int write_offset_adjacency(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[65536];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        long long src_id = atoll(line);
        fprintf(out, "%" PRIu64 ":", (uint64_t)src_id + offset);

        char *neighbors = colon + 1;
        char *token = strtok(neighbors, ",\r\n");
        int first = 1;
        while (token) {
            long long dst_id = atoll(token);
            if (!first) fprintf(out, ",");
            fprintf(out, "%" PRIu64, (uint64_t)dst_id + offset);
            first = 0;
            token = strtok(NULL, ",\r\n");
        }
        fprintf(out, "\n");
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Write offsetted edge list file (creates dst) */
static int write_offset_edges(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        long long src_id, dst_id;
        if (sscanf(line, "%lld %lld", &src_id, &dst_id) == 2) {
            fprintf(out, "%" PRIu64 " %" PRIu64 "\n",
                    (uint64_t)src_id + offset, (uint64_t)dst_id + offset);
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Write offsetted URL map file (creates dst) */
static int write_offset_url_map(const char *src, const char *dst, uint64_t offset) {
    FILE *in = fopen(src, "r");
    FILE *out = fopen(dst, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char line[8192];
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#') continue;

        long long node_id;
        char url[7000];
        if (sscanf(line, "%lld: %6999s", &node_id, url) == 2) {
            fprintf(out, "%" PRIu64 ": %s\n", (uint64_t)node_id + offset, url);
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Append file src to dst (dst is created if missing) */
static int append_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "ab");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }

    fclose(in);
    fclose(out);
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
    printf("  Milestone 3 - Incremental Graph Growth (Aleena)\n");
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
       PHASE 1 — Compute PageRank Directly (Skip Graph Struct)
       Memory-Efficient: Loads only into ParallelGraph for PageRank
       ========================================================== */
    printf("\n========================================================\n");
    printf("  PHASE 1: PageRank Computation (File-Based)\n");
    printf("========================================================\n");

    /* Check if phase1_graph.adj exists */
    FILE *phase1_check = fopen("phase1_graph.adj", "r");
    if (!phase1_check) {
        fprintf(stderr, "\nERROR: phase1_graph.adj not found!\n");
        fprintf(stderr, "Use graph500_to_phase1_converter.exe to generate it:\n");
        fprintf(stderr, "  ./graph500_to_phase1_converter.exe -v graph500-22.v -e graph500-22.e\n");
        fprintf(stderr, "Or run normal web crawling pipeline first.\n\n");
        return 1;
    }
    fclose(phase1_check);

    printf("[Phase 1] Loading phase1_graph.adj for PageRank computation...\n");

    /* Load directly into ParallelGraph (avoids duplicate Graph struct in memory) */
    ParallelGraph *pg = parallel_load_graph("phase1_graph.adj");
    if (!pg) {
        fprintf(stderr, "Failed to load phase1_graph.adj for PageRank\n");
        return 1;
    }

    /* Get stats from ParallelGraph */
    nodes_before = pg->num_nodes;
    edges_before = pg->num_edges;
    printf("[Phase 1] Graph loaded: %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_before, edges_before);

    printf("[PageRank] Running parallel PageRank on Phase 1 graph "
           "(%d nodes, %d threads)...\n", pg->num_nodes, cfg.threads);

    parallel_pagerank_compute(pg, cfg.threads);
    parallel_pagerank_save_ranks(pg, "phase1_ranks.txt");
    parallel_pagerank_print_ranks(pg);
    printf("[PageRank] Phase 1 ranks saved to phase1_ranks.txt\n");

    /* Save Phase 1 stats before freeing the graph */
    nodes_before = pg->num_nodes;
    edges_before = pg->num_edges;

    parallel_free_graph(pg);

    /* Verify phase1_ranks.txt was created */
    FILE *ranks_check = fopen("phase1_ranks.txt", "r");
    if (ranks_check) {
        fclose(ranks_check);
        printf("[Phase 1] [OK] Phase 1 ranks computed and saved\n");
    } else {
        fprintf(stderr, "ERROR: Failed to create phase1_ranks.txt\n");
        return 1;
    }

    /* ==========================================================
       PHASE 2 — Extend Phase 1: Append New Pages to Union Graph
       The union is created by copying Phase 1 files + appending crawl
       ========================================================== */
    printf("\n========================================================\n");
    printf("  PHASE 2: Extend Phase 1 Graph with New Crawled Pages\n");
    printf("========================================================\n");

    /* Step 1: Copy Phase 1 files to Phase 2 */
    printf("[Phase 2] Step 1: Copying Phase 1 graph to Phase 2 files...\n");
    if (copy_file("phase1_graph.adj", "phase2_graph.adj") != 0 ||
        copy_file("phase1_graph.edge", "phase2_graph.edge") != 0 ||
        copy_file("phase1_url_map.txt", "phase2_url_map.txt") != 0) {
        fprintf(stderr, "ERROR: Failed to copy Phase 1 graph files\n");
        return 1;
    }
    printf("[Phase 2] Step 1: Copied Phase 1 graph [OK]\n");

    /* Step 2: Keep Phase 1 on disk; crawl only new seeds in memory */
    printf("[Phase 2] Step 2: Phase 1 remains on disk (no in-memory load)\n");

    /* Record Phase 1 stats before crawling Phase 2 */
    uint64_t phase1_node_count = nodes_before;
    uint64_t phase1_edge_count = edges_before;
    printf("[Phase 2] Phase 1 baseline: %" PRIu64 " nodes, %" PRIu64 " edges\n",
           phase1_node_count, phase1_edge_count);
    printf("[Phase 2] New nodes will start from ID: %" PRIu64 "\n", phase1_node_count);

    /* Step 3: Create fresh graph for Phase 2 union crawling */
    printf("[Phase 2] Step 3: Creating graph for union crawling...\n");
    if (graph) graph_destroy(graph);  /* Free Phase 1 if still in memory */
    graph = graph_create();
    if (!graph) {
        fprintf(stderr, "ERROR: Failed to create graph for Phase 2\n");
        return 1;
    }

    /* Step 4: Prepare frontier for Phase 2 crawling */
    printf("[Phase 2] Step 4: Setting up frontier for Phase 2 crawling...\n");
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

    uint64_t new_nodes = 0, new_edges = 0;
    graph_get_stats(graph, &new_nodes, &new_edges);
    nodes_after = phase1_node_count + new_nodes;
    edges_after = phase1_edge_count + new_edges;
    printf("\n[Phase 2] Crawl complete using %d worker threads\n", cfg.threads);
        printf("[Phase 2] Union graph: %" PRIu64 " total nodes (Phase1: %" PRIu64", Phase2 new: %" PRIu64 ")\n",
            nodes_after, phase1_node_count, new_nodes);
    printf("[Phase 2] Union graph: %" PRIu64 " total edges (%.0f ms)\n",
           edges_after, p2_elapsed);
    printf("[Phase 2] IMPORTANT: Phase2 graph now contains BOTH Phase1 AND new nodes\n");
    printf("[Phase 2]           Nodes 0-%" PRIu64 ": Phase 1 original nodes\n", phase1_node_count - 1);
    printf("[Phase 2]           Nodes %" PRIu64 "-%" PRIu64 ": Phase 2 new nodes\n\n",
           phase1_node_count, nodes_after - 1);

    /* Save only newly crawled subgraph, then append into copied Phase 2 files with offset */
    if (graph_save(graph, "phase2_new_graph.adj", "adjacency") != 0 ||
        graph_save(graph, "phase2_new_graph.edge", "edgelist") != 0 ||
        graph_save_url_map(graph, "phase2_new_url_map.txt") != 0) {
        fprintf(stderr, "ERROR: Failed to save Phase 2 new subgraph files\n");
        return 1;
    }
    /* Create offsetted versions of the new subgraph files so their
       node IDs start from phase1_node_count instead of 0. Then append
       the offset files into the copied Phase 1 union files. */
    if (write_offset_adjacency("phase2_new_graph.adj", "phase2_new_graph.offset.adj", phase1_node_count) != 0 ||
        write_offset_edges("phase2_new_graph.edge", "phase2_new_graph.offset.edge", phase1_node_count) != 0 ||
        write_offset_url_map("phase2_new_url_map.txt", "phase2_new_url_map.offset.txt", phase1_node_count) != 0) {
        fprintf(stderr, "ERROR: Failed to create offsetted Phase 2 new subgraph files\n");
        return 1;
    }

    /* Replace originals with offset versions */
    if (remove("phase2_new_graph.adj") != 0 || rename("phase2_new_graph.offset.adj", "phase2_new_graph.adj") != 0) {
        fprintf(stderr, "WARNING: Failed to replace phase2_new_graph.adj with offset version\n");
    }
    if (remove("phase2_new_graph.edge") != 0 || rename("phase2_new_graph.offset.edge", "phase2_new_graph.edge") != 0) {
        fprintf(stderr, "WARNING: Failed to replace phase2_new_graph.edge with offset version\n");
    }
    if (remove("phase2_new_url_map.txt") != 0 || rename("phase2_new_url_map.offset.txt", "phase2_new_url_map.txt") != 0) {
        fprintf(stderr, "WARNING: Failed to replace phase2_new_url_map.txt with offset version\n");
    }

    if (append_file("phase2_new_graph.adj", "phase2_graph.adj") != 0 ||
        append_file("phase2_new_graph.edge", "phase2_graph.edge") != 0 ||
        append_file("phase2_new_url_map.txt", "phase2_url_map.txt") != 0) {
        fprintf(stderr, "ERROR: Failed to append Phase 2 new subgraph into union graph files\n");
        return 1;
    }

    /* Save phase1_node_count for incremental PageRank strategy */
    FILE *meta = fopen("phase2_metadata.txt", "w");
    if (meta) {
        fprintf(meta, "phase1_node_count: %" PRIu64 "\n", phase1_node_count);
        fprintf(meta, "phase1_edge_count: %" PRIu64 "\n", phase1_edge_count);
        fprintf(meta, "phase2_total_nodes: %" PRIu64 "\n", nodes_after);
        fprintf(meta, "phase2_total_edges: %" PRIu64 "\n", edges_after);
        fclose(meta);
        printf("[Phase 2] Saved metadata to phase2_metadata.txt\n");
    }

    /* ==========================================================
       SUMMARY
       ========================================================== */
    printf("\n========================================================\n");
    printf("  WORKFLOW SUMMARY\n");
    printf("========================================================\n");
    printf("\n  PHASE 1 (Pre-computed base graph):\n");
    printf("    - Loaded & Ranked: %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_before, edges_before);
    printf("\n  PHASE 2 (Extended Phase 1 with new crawled pages):\n");
    printf("    - New discovered: %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_after - phase1_node_count, edges_after - phase1_edge_count);
    printf("    - Total union: %" PRIu64 " nodes, %" PRIu64 " edges\n",
           nodes_after, edges_after);
    printf("\n  UNION GRAPH ARCHITECTURE:\n");
    printf("    - Node IDs 0-%" PRIu64 ": Original Phase 1 nodes\n", phase1_node_count - 1);
    printf("    - Node IDs %" PRIu64 "-%" PRIu64 ": Newly discovered Phase 2 nodes\n",
           phase1_node_count, nodes_after - 1);
    printf("    - Phase 2 graph files CONTAIN THE COMPLETE UNION\n");
    printf("    - Incremental PageRank: Initialize old nodes from Phase 1 ranks\n");
    printf("    - Static PageRank: Recompute from scratch on full graph\n");
    printf("    - Phase 2 crawl time: %.0f ms\n", p2_elapsed);
    printf("========================================================\n");
    printf("\n  Output files:\n");
    printf("    phase1_graph.adj / .edge   - Phase 1 base graph only\n");
    printf("    phase1_ranks.txt           - Phase 1 PageRank scores (for warm-start)\n");
    printf("    phase1_url_map.txt         - URL-to-ID mapping (phase 1)\n");
    printf("    phase2_graph.adj / .edge   - COMPLETE UNION (copy + appended offset nodes)\n");
    printf("    phase2_url_map.txt         - URL-to-ID mapping (complete union)\n");
    printf("    phase2_metadata.txt        - Node count info for incremental PageRank\n");
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
