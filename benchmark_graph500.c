/*
 * benchmark_graph500.c
 *
 * Abdurrehman-style benchmark harness for Aleena's four PageRank strategies,
 * running on the graph500-22 large dataset.
 *
 * Dataset  : graph500-22  (2,396,657 nodes, 64,155,735 edges)
 * Format   : edge-list  "src dst\n"  (undirected — we add both directions)
 * Reference: graph500-22.properties
 *
 * Build:
 *   gcc -Wall -Wextra -pthread -O2 -o benchmark_graph500.exe \
 *       benchmark_graph500.c pagerank_strategies.c pagerank.c -lm -lpthread
 *
 * Usage:
 *   benchmark_graph500.exe [edge_file] [threads] [max_iters] [threshold]
 *
 *   Defaults:
 *     edge_file  = graph500-22/graph500-22.e
 *     threads    = 4
 *     max_iters  = 10   (matches graph500 PR spec: num-iterations = 10)
 *     threshold  = 1e-6
 */

#include "pagerank_strategies.h"
#include "pagerank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#  include <windows.h>
static double wall_ms(void) { return (double)GetTickCount64(); }
#else
#  include <sys/time.h>
static double wall_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

/* ---------------------------------------------------------------
   Edge-list loader for graph500 ".e" files
   Format: one "src dst" pair per line, 0-indexed integer IDs.
   The dataset is undirected, so we store both directions.
   --------------------------------------------------------------- */
static PageRankGraph *load_edgelist(const char *filename) {
    printf("[Loader] Opening %s ...\n", filename);
    fflush(stdout);

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[Loader] ERROR: cannot open %s\n", filename);
        return NULL;
    }

    /* --- Pass 1: find max node id and count edges --- */
    long   edge_count = 0;
    int    max_id     = -1;
    int    u, v;

    while (fscanf(f, "%d %d", &u, &v) == 2) {
        if (u > max_id) max_id = u;
        if (v > max_id) max_id = v;
        edge_count++;
        if (edge_count % 5000000L == 0) {
            printf("[Loader]   scanned %ld M edges ...\n", edge_count / 1000000L);
            fflush(stdout);
        }
    }

    int N = max_id + 1;
    printf("[Loader] Pass 1 done: %d nodes, %ld edges (before undirected expansion)\n",
           N, edge_count);
    fflush(stdout);

    /* --- Allocate degree arrays --- */
    int *out_deg = calloc(N, sizeof(int));
    if (!out_deg) { fclose(f); fprintf(stderr,"[Loader] OOM\n"); return NULL; }

    /* --- Pass 2: count per-node out-degree (both directions) --- */
    rewind(f);
    long scanned = 0;
    while (fscanf(f, "%d %d", &u, &v) == 2) {
        out_deg[u]++;   /* u -> v */
        out_deg[v]++;   /* v -> u  (undirected) */
        scanned++;
        if (scanned % 5000000L == 0) {
            printf("[Loader]   degree count %ld M ...\n", scanned / 1000000L);
            fflush(stdout);
        }
    }

    /* --- Allocate adjacency arrays --- */
    int **outlinks = malloc(N * sizeof(int *));
    if (!outlinks) { free(out_deg); fclose(f); fprintf(stderr,"[Loader] OOM\n"); return NULL; }

    for (int i = 0; i < N; i++) {
        if (out_deg[i] > 0) {
            outlinks[i] = malloc(out_deg[i] * sizeof(int));
            if (!outlinks[i]) {
                fprintf(stderr, "[Loader] OOM at node %d\n", i);
                /* clean up and abort */
                for (int j = 0; j < i; j++) free(outlinks[j]);
                free(outlinks); free(out_deg); fclose(f);
                return NULL;
            }
        } else {
            outlinks[i] = NULL;
        }
    }

    /* reuse out_deg as fill pointer */
    int *fill = calloc(N, sizeof(int));
    if (!fill) {
        for (int i = 0; i < N; i++) free(outlinks[i]);
        free(outlinks); free(out_deg); fclose(f);
        fprintf(stderr,"[Loader] OOM\n"); return NULL;
    }

    /* --- Pass 3: fill adjacency lists --- */
    rewind(f);
    scanned = 0;
    while (fscanf(f, "%d %d", &u, &v) == 2) {
        outlinks[u][fill[u]++] = v;
        outlinks[v][fill[v]++] = u;
        scanned++;
        if (scanned % 5000000L == 0) {
            printf("[Loader]   filling %ld M ...\n", scanned / 1000000L);
            fflush(stdout);
        }
    }
    fclose(f);
    free(fill);

    printf("[Loader] Pass 3 done. Building PageRankGraph ...\n");
    fflush(stdout);

    /* --- Build PageRankGraph struct --- */
    PageRankGraph *pg = malloc(sizeof(PageRankGraph));
    if (!pg) {
        for (int i = 0; i < N; i++) free(outlinks[i]);
        free(outlinks); free(out_deg);
        fprintf(stderr,"[Loader] OOM\n"); return NULL;
    }

    pg->num_nodes      = N;
    pg->outlink_counts = out_deg;   /* transfer ownership */
    pg->outlinks       = outlinks;
    pg->ranks          = malloc(N * sizeof(double));
    pg->new_ranks      = malloc(N * sizeof(double));

    if (!pg->ranks || !pg->new_ranks) {
        fprintf(stderr,"[Loader] OOM allocating rank arrays\n");
        pagerank_free(pg);
        return NULL;
    }

    /* count real edges for reporting */
    long total_dir_edges = 0;
    for (int i = 0; i < N; i++) total_dir_edges += out_deg[i];

    printf("[Loader] Graph ready: %d nodes, %ld directed edges\n\n",
           N, total_dir_edges);
    fflush(stdout);

    return pg;
}

/* ---------------------------------------------------------------
   Abdurrehman-style performance report header (matching his
   performance_analysis.md layout)
   --------------------------------------------------------------- */
static void print_bench_header(int N, long edges, int threads,
                               int max_iters, double threshold) {
    printf("============================================================\n");
    printf("  ABDURREHMAN BENCHMARK x ALEENA STRATEGIES — graph500-22\n");
    printf("============================================================\n");
    printf("  Graph    : graph500-22  (LDBC Graphalytics)\n");
    printf("  Nodes    : %d\n", N);
    printf("  Directed edges: %ld\n", edges);
    printf("  Threads  : %d\n", threads);
    printf("  Max iters: %d  |  Threshold: %.0e\n", max_iters, threshold);
    printf("  Damping  : %.2f\n", DAMPING_FACTOR);
    printf("============================================================\n\n");
}

/* ---------------------------------------------------------------
   Main
   --------------------------------------------------------------- */
int main(int argc, char **argv) {

    const char *edge_file  = "graph500-22/graph500-22.e";
    int         threads    = 4;
    int         max_iters  = 10;   /* graph500 PR spec default */
    double      threshold  = 1e-6;

    if (argc > 1) edge_file  = argv[1];
    if (argc > 2) threads    = atoi(argv[2]);
    if (argc > 3) max_iters  = atoi(argv[3]);
    if (argc > 4) threshold  = atof(argv[4]);

    if (threads < 1) threads = 1;

    /* ---- Load dataset ---- */
    double t0 = wall_ms();
    PageRankGraph *pg = load_edgelist(edge_file);
    if (!pg) return 1;
    double load_ms = wall_ms() - t0;
    printf("[Benchmark] Graph loaded in %.2f ms\n\n", load_ms);

    /* Count directed edges */
    long total_edges = 0;
    for (int i = 0; i < pg->num_nodes; i++) total_edges += pg->outlink_counts[i];

    print_bench_header(pg->num_nodes, total_edges, threads, max_iters, threshold);

    /* ---- Run all four strategies (Aleena's comparison driver) ---- */
    pagerank_run_comparison(pg, threads, max_iters, threshold,
                            "graph500_strategy_results.txt");

    /* ---- Abdurrehman-style speedup sweep: 1,2,4,8 threads ---- */
    printf("\n============================================================\n");
    printf("  THREAD SCALABILITY SWEEP  (Distributed+Convergence)\n");
    printf("  (Mirrors Abdurrehman performance_analysis.md table)\n");
    printf("============================================================\n");
    printf("%-16s | %10s | %7s | %10s\n",
           "Threads", "Time (ms)", "Speedup", "Efficiency");
    printf("-----------------+------------+---------+------------\n");

    int thread_counts[] = {1, 2, 4, 8};
    double base_time    = 0.0;

    for (int ti = 0; ti < 4; ti++) {
        int t = thread_counts[ti];
        StrategyResult r = pagerank_distributed_convergence(pg, t, threshold);

        if (ti == 0) base_time = r.elapsed_ms;

        double speedup    = (base_time > 0.0) ? base_time / r.elapsed_ms : 1.0;
        double efficiency = speedup / t * 100.0;

        printf("%-16d | %10.2f | %6.2fx | %9.1f%%\n",
               t, r.elapsed_ms, speedup, efficiency);
        fflush(stdout);
    }

    /* ---- Communication overhead estimate (Distributed vs Centralized) ---- */
    printf("\n============================================================\n");
    printf("  COMMUNICATION OVERHEAD ANALYSIS\n");
    printf("============================================================\n");

    /* Centralized strategy is skipped for graph500-22 (N > 2M nodes).
       Per-node mutexes cause O(N) lock acquisitions per dangling node per iteration —
       completely impractical at this scale. Overhead is characterised by memory layout
       and the distributed vs centralized comparison on smaller graphs instead. */
    printf("  NOTE: Centralized strategy NOT run on graph500-22 (N=%d).\n", pg->num_nodes);
    printf("  Per-node mutex contention at 2M+ nodes would take hours.\n");
    printf("  See crawled_graph results (438K nodes) for centralized vs distributed comparison.\n\n");

    StrategyResult distrib  = pagerank_distributed_convergence(pg, threads, threshold);

    double data_mb = (double)pg->num_nodes * sizeof(double) / (1024.0 * 1024.0);

    printf("  Distributed time  : %.2f ms\n", distrib.elapsed_ms);
    printf("  ranks[] data      : %.2f MB\n", data_mb);
    printf("  new_ranks[] data  : %.2f MB\n", data_mb);
    printf("  Total shared mem  : %.2f MB\n", 2.0 * data_mb);
    printf("  Thread buffers(%d x): %.2f MB\n", threads, threads * data_mb);
    printf("  Mutex overhead    : N/A (centralized not run at this scale)\n");
    printf("============================================================\n\n");

    pagerank_free(pg);

    printf("[Benchmark] All done. Results written to graph500_strategy_results.txt\n");
    return 0;
}
