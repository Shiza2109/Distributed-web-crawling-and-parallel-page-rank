#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#include "frontier.h"
#include "crawl_policy.h"
#include "worker.h"
#include "manual_worker.h"
#include "graph.h"
#include "fetch.h"
#include "reproducibility.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static double now_ms(void) { return (double)GetTickCount64(); }
#else
#include <time.h>
#include <unistd.h>
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
#endif

typedef enum {
    MODE_COMPARE,
    MODE_THREADED,
    MODE_MANUAL
} RunMode;

typedef enum {
    PROFILE_SMALL,
    PROFILE_LARGE
} SeedProfile;

typedef struct {
    RunMode mode;
    SeedProfile profile;
    int threads;
    int max_depth;
    uint64_t max_pages;
} BenchConfig;

typedef struct {
    double elapsed_ms;
    uint64_t nodes;
    uint64_t edges;
    uint64_t discovered;
    uint64_t fetched;
    int seeds_added;
} CrawlRunResult;

static const char *SMALL_SEEDS[] = {
    "http://example.com",
    "http://example.org",
    "https://www.python.org/",
    "https://www.gnu.org/",
    "https://www.wikipedia.org/",
    "https://github.com/",
    "https://stackoverflow.com/",
    "https://www.bbc.com/"
};

/*
 * A larger and varied seed set to create realistic network-bound crawl work.
 * Most URLs are stable docs/news/home pages to avoid excessive 404 noise.
 */
static const char *LARGE_SEEDS[] = {
    "http://example.com",
    "http://example.org",
    "https://www.iana.org/",
    "https://www.wikipedia.org/",
    "https://en.wikipedia.org/wiki/Main_Page",
    "https://www.gnu.org/",
    "https://www.kernel.org/",
    "https://www.python.org/",
    "https://docs.python.org/3/",
    "https://www.openbsd.org/",
    "https://www.freebsd.org/",
    "https://www.debian.org/",
    "https://ubuntu.com/",
    "https://archlinux.org/",
    "https://www.linux.org/",
    "https://www.rust-lang.org/",
    "https://doc.rust-lang.org/",
    "https://go.dev/",
    "https://pkg.go.dev/",
    "https://nodejs.org/",
    "https://www.java.com/",
    "https://openjdk.org/",
    "https://www.postgresql.org/",
    "https://www.mysql.com/",
    "https://sqlite.org/",
    "https://redis.io/",
    "https://www.mongodb.com/",
    "https://www.nginx.com/",
    "https://httpd.apache.org/",
    "https://www.cloudflare.com/",
    "https://www.mozilla.org/",
    "https://developer.mozilla.org/",
    "https://www.w3.org/",
    "https://www.w3schools.com/",
    "https://www.ibm.com/",
    "https://www.oracle.com/",
    "https://www.intel.com/",
    "https://www.nvidia.com/",
    "https://www.amd.com/",
    "https://www.microsoft.com/",
    "https://learn.microsoft.com/",
    "https://github.com/",
    "https://docs.github.com/",
    "https://git-scm.com/",
    "https://about.gitlab.com/",
    "https://stackoverflow.com/",
    "https://serverfault.com/",
    "https://superuser.com/",
    "https://www.reddit.com/",
    "https://news.ycombinator.com/",
    "https://www.bbc.com/",
    "https://www.cnn.com/",
    "https://www.reuters.com/",
    "https://www.nytimes.com/",
    "https://www.theguardian.com/",
    "https://www.aljazeera.com/",
    "https://www.economist.com/",
    "https://www.nasa.gov/",
    "https://www.noaa.gov/",
    "https://www.nist.gov/",
    "https://www.who.int/",
    "https://www.un.org/",
    "https://www.worldbank.org/",
    "https://www.imf.org/",
    "https://www.oecd.org/",
    "https://www.cern.ch/",
    "https://home.cern/",
    "https://arxiv.org/",
    "https://www.nature.com/",
    "https://www.science.org/",
    "https://www.springer.com/",
    "https://www.elsevier.com/",
    "https://www.acm.org/",
    "https://www.ieee.org/",
    "https://www.kaggle.com/",
    "https://www.tensorflow.org/",
    "https://pytorch.org/",
    "https://scikit-learn.org/",
    "https://numpy.org/",
    "https://pandas.pydata.org/",
    "https://matplotlib.org/",
    "https://plotly.com/",
    "https://jupyter.org/",
    "https://www.anaconda.com/",
    "https://www.docker.com/",
    "https://kubernetes.io/",
    "https://helm.sh/",
    "https://prometheus.io/",
    "https://grafana.com/",
    "https://www.jenkins.io/",
    "https://circleci.com/",
    "https://www.travis-ci.com/",
    "https://www.netlify.com/",
    "https://vercel.com/",
    "https://www.heroku.com/",
    "https://aws.amazon.com/",
    "https://cloud.google.com/",
    "https://azure.microsoft.com/",
    "https://www.digitalocean.com/",
    "https://www.linode.com/",
    "https://www.cloudflarestatus.com/",
    "https://status.github.com/",
    "https://www.wikipedia.org/wiki/Computer_science",
    "https://www.wikipedia.org/wiki/Web_crawler",
    "https://www.wikipedia.org/wiki/PageRank"
};

static void print_usage(const char *prog) {
    printf("Usage: %s [--mode compare|threaded|manual] [--profile small|large] [--threads N] [--depth N] [--max-pages N]\n", prog);
    printf("Defaults: --mode compare --profile large --threads 8 --depth 0 --max-pages 500\n");
    printf("Tip: depth=0 isolates fetch throughput; depth=1 adds crawler expansion behavior.\n");
}

static int parse_args(int argc, char **argv, BenchConfig *cfg) {
    cfg->mode = MODE_COMPARE;
    cfg->profile = PROFILE_LARGE;
    cfg->threads = 8;
    cfg->max_depth = 0;
    cfg->max_pages = 500;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "compare") == 0) cfg->mode = MODE_COMPARE;
            else if (strcmp(argv[i], "threaded") == 0) cfg->mode = MODE_THREADED;
            else if (strcmp(argv[i], "manual") == 0) cfg->mode = MODE_MANUAL;
            else return -1;
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "small") == 0) cfg->profile = PROFILE_SMALL;
            else if (strcmp(argv[i], "large") == 0) cfg->profile = PROFILE_LARGE;
            else return -1;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            cfg->threads = atoi(argv[++i]);
            if (cfg->threads <= 0 || cfg->threads > WORKER_MAX_THREADS) return -1;
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            cfg->max_depth = atoi(argv[++i]);
            if (cfg->max_depth < 0) return -1;
        } else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc) {
            cfg->max_pages = (uint64_t)strtoull(argv[++i], NULL, 10);
            if (cfg->max_pages == 0) return -1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return -1;
        } else {
            return -1;
        }
    }

    return 0;
}

static const char **select_seeds(SeedProfile profile, int *out_count) {
    if (profile == PROFILE_SMALL) {
        *out_count = (int)(sizeof(SMALL_SEEDS) / sizeof(SMALL_SEEDS[0]));
        return SMALL_SEEDS;
    }
    *out_count = (int)(sizeof(LARGE_SEEDS) / sizeof(LARGE_SEEDS[0]));
    return LARGE_SEEDS;
}

static int load_seeds(Frontier *fr, const char **seeds, int n) {
    int added = 0;
    for (int i = 0; i < n; i++) {
        added += frontier_push(fr, seeds[i], 0) ? 1 : 0;
    }
    return added;
}

static int run_threaded_crawl(Frontier *frontier, Graph *graph, int n_threads) {
    WorkerPool pool;
    if (worker_pool_start(&pool, frontier, graph, n_threads) != 0) {
        return -1;
    }

    int stable_idle_checks = 0;
    while (stable_idle_checks < 3) {
        uint64_t discovered, fetched;
        int count;

        pthread_mutex_lock(&frontier->lock);
        count = frontier->count;
        discovered = frontier->total_discovered;
        fetched = frontier->total_fetched;
        pthread_mutex_unlock(&frontier->lock);

        if (count == 0 && discovered > 0 && fetched >= discovered) {
            stable_idle_checks++;
        } else {
            stable_idle_checks = 0;
        }
        sleep_ms(200);
    }

    frontier_shutdown(frontier);
    worker_pool_join(&pool);
    return 0;
}

static int run_single(const BenchConfig *cfg, const char **seeds, int seed_count,
                      int run_threads, const char *tag, int persist_outputs,
                      CrawlRunResult *out) {
    CrawlPolicy *policy = policy_create(cfg->max_depth, cfg->max_pages, 0);
    Frontier frontier;
    Graph *graph;
    double start_ms, end_ms;

    if (!policy) return -1;

    frontier_init(&frontier, policy);
    graph = graph_create();
    if (!graph) {
        frontier_destroy(&frontier);
        policy_destroy(policy);
        return -1;
    }

    policy_set_seed_domains(policy, (char **)seeds, seed_count);
    out->seeds_added = load_seeds(&frontier, seeds, seed_count);

    printf("\n[%s] Seeds requested: %d, added to frontier: %d\n", tag, seed_count, out->seeds_added);

    start_ms = now_ms();
    if (run_threads > 1) {
        if (run_threaded_crawl(&frontier, graph, run_threads) != 0) {
            graph_destroy(graph);
            frontier_destroy(&frontier);
            policy_destroy(policy);
            return -1;
        }
    } else {
        if (run_manual_crawl(&frontier, graph) != 0) {
            graph_destroy(graph);
            frontier_destroy(&frontier);
            policy_destroy(policy);
            return -1;
        }
    }
    end_ms = now_ms();

    graph_get_stats(graph, &out->nodes, &out->edges);
    out->discovered = frontier.total_discovered;
    out->fetched = frontier.total_fetched;
    out->elapsed_ms = end_ms - start_ms;

    if (persist_outputs) {
        if (run_threads > 1) {
            graph_save(graph, "crawled_graph_threaded.adj", "adjacency");
            graph_save(graph, "crawled_graph_threaded.edge", "edgelist");
            graph_save_url_map(graph, "url_map_threaded.txt");
        } else {
            graph_save_url_map(graph, "url_map_manual.txt");
        }
    }

    graph_destroy(graph);
    frontier_destroy(&frontier);
    policy_destroy(policy);
    return 0;
}

static void append_combined_manifest(const BenchConfig *cfg,
                                     const char **seeds,
                                     int seed_count,
                                     int ran_threaded,
                                     const CrawlRunResult *threaded_res,
                                     int ran_manual,
                                     const CrawlRunResult *manual_res,
                                     double speedup) {
    time_t now = time(NULL);
    struct tm tm_info;
    char timestamp[64];
    FILE *f;
    uint64_t seed_hash;

#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);
    seed_hash = manifest_compute_hash(seeds, seed_count);

    f = fopen("manifest.txt", "a");
    if (!f) {
        fprintf(stderr, "Failed to append manifest.txt\n");
        return;
    }

    /* Separator line before each newly appended run. */
    fprintf(f, "------------------------------------------------------------\n");
    fprintf(f, "timestamp=%s\n", timestamp);
    fprintf(f, "mode=%s\n", cfg->mode == MODE_COMPARE ? "compare" :
                             (cfg->mode == MODE_THREADED ? "threaded" : "manual"));
    fprintf(f, "profile=%s\n", cfg->profile == PROFILE_LARGE ? "large" : "small");
    fprintf(f, "threads=%d\n", cfg->threads);
    fprintf(f, "max_depth=%d\n", cfg->max_depth);
    fprintf(f, "max_pages=%" PRIu64 "\n", cfg->max_pages);
    fprintf(f, "seed_hash=%" PRIu64 "\n", seed_hash);

    if (ran_threaded && threaded_res) {
        fprintf(f, "\n[THREADED]\n");
        fprintf(f, "Elapsed: %.2f ms\n", threaded_res->elapsed_ms);
        fprintf(f, "Seeds added: %d\n", threaded_res->seeds_added);
        fprintf(f, "Discovered: %" PRIu64 "\n", threaded_res->discovered);
        fprintf(f, "Fetched: %" PRIu64 "\n", threaded_res->fetched);
        fprintf(f, "Graph nodes: %" PRIu64 "\n", threaded_res->nodes);
        fprintf(f, "Graph edges: %" PRIu64 "\n", threaded_res->edges);
    }

    if (ran_manual && manual_res) {
        fprintf(f, "\n[MANUAL]\n");
        fprintf(f, "Elapsed: %.2f ms\n", manual_res->elapsed_ms);
        fprintf(f, "Seeds added: %d\n", manual_res->seeds_added);
        fprintf(f, "Discovered: %" PRIu64 "\n", manual_res->discovered);
        fprintf(f, "Fetched: %" PRIu64 "\n", manual_res->fetched);
        fprintf(f, "Graph nodes: %" PRIu64 "\n", manual_res->nodes);
        fprintf(f, "Graph edges: %" PRIu64 "\n", manual_res->edges);
    }

    if (cfg->mode == MODE_COMPARE && ran_threaded && ran_manual) {
        fprintf(f, "\n[COMPARISON]\n");
        fprintf(f, "Threaded time: %.2f ms\n", threaded_res->elapsed_ms);
        fprintf(f, "Manual time: %.2f ms\n", manual_res->elapsed_ms);
        fprintf(f, "Speedup (manual/threaded): %.2fx\n", speedup);
    }

    fprintf(f, "\n");
    fclose(f);
}

static void print_summary(const char *label, const CrawlRunResult *r) {
    printf("\n=== %s ===\n", label);
    printf("Elapsed: %.2f ms\n", r->elapsed_ms);
    printf("Seeds added: %d\n", r->seeds_added);
    printf("Discovered: %" PRIu64 "\n", r->discovered);
    printf("Fetched: %" PRIu64 "\n", r->fetched);
    printf("Graph nodes: %" PRIu64 "\n", r->nodes);
    printf("Graph edges: %" PRIu64 "\n", r->edges);
}

int main(int argc, char **argv) {
    BenchConfig cfg;
    CrawlRunResult threaded_res, manual_res;
    const char **seeds;
    int seed_count;
    int ran_threaded = 0;
    int ran_manual = 0;
    double speedup = 0.0;

    if (parse_args(argc, argv, &cfg) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    seeds = select_seeds(cfg.profile, &seed_count);

    printf("==============================================\n");
    printf(" Web Crawler Benchmark / Integration Runner\n");
    printf("==============================================\n");
    printf("Mode: %s\n", cfg.mode == MODE_COMPARE ? "compare" : (cfg.mode == MODE_THREADED ? "threaded" : "manual"));
    printf("Profile: %s (%d URLs)\n", cfg.profile == PROFILE_LARGE ? "large" : "small", seed_count);
    printf("Threads: %d\n", cfg.threads);
    printf("Max depth: %d\n", cfg.max_depth);
    printf("Max pages: %" PRIu64 "\n", cfg.max_pages);

    fetch_global_init();

    if (cfg.mode == MODE_THREADED || cfg.mode == MODE_COMPARE) {
        if (run_single(&cfg, seeds, seed_count, cfg.threads, "THREADED", 1, &threaded_res) != 0) {
            fetch_global_cleanup();
            fprintf(stderr, "Threaded run failed.\n");
            return 1;
        }
        ran_threaded = 1;
        print_summary("THREADED", &threaded_res);
    }

    if (cfg.mode == MODE_MANUAL || cfg.mode == MODE_COMPARE) {
        if (run_single(&cfg, seeds, seed_count, 1, "MANUAL", 1, &manual_res) != 0) {
            fetch_global_cleanup();
            fprintf(stderr, "Manual run failed.\n");
            return 1;
        }
        ran_manual = 1;
        print_summary("MANUAL", &manual_res);
    }

    if (cfg.mode == MODE_COMPARE) {
        if (threaded_res.elapsed_ms > 0.0) {
            speedup = manual_res.elapsed_ms / threaded_res.elapsed_ms;
        }
        printf("\n=== COMPARISON ===\n");
        printf("Threaded time: %.2f ms\n", threaded_res.elapsed_ms);
        printf("Manual time: %.2f ms\n", manual_res.elapsed_ms);
        printf("Speedup (manual/threaded): %.2fx\n", speedup);
    }

    append_combined_manifest(&cfg,
                             seeds, seed_count,
                             ran_threaded, &threaded_res,
                             ran_manual, &manual_res,
                             speedup);

    fetch_global_cleanup();

    printf("\nOutputs generated:\n");
    printf("- crawled_graph_threaded.adj/.edge + url_map_threaded.txt\n");
    printf("- manual mode: url_map_manual.txt (no manual graph files)\n");
    printf("- manifest.txt (combined, appended per run)\n");

    return 0;
}