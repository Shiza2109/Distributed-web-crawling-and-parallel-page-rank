/*
 * benchmark_small_datasets.c
 *
 * Benchmark harness for Aleena's four PageRank strategies on:
 *   - wiki-Talk   : 2,394,385 nodes, 5,021,410 directed edges
 *   - cit-Patents : 3,774,768 nodes, 16,518,948 directed edges
 *
 * Both datasets are DIRECTED (unlike graph500-22 which was undirected).
 * So edges are loaded as-is, no reverse edge added.
 *
 * Build:
 *   gcc -Wall -Wextra -pthread -O2 -o benchmark_small.exe \
 *       benchmark_small_datasets.c pagerank_strategies.c pagerank.c -lm -lpthread
 *
 * Usage:
 *   benchmark_small.exe <edge_file> <dataset_name> [threads] [max_iters] [threshold]
 *
 * Examples:
 *   benchmark_small.exe wiki-Talk/wiki-Talk.e       wiki-Talk    4 10 1e-4
 *   benchmark_small.exe citi-patents/cit-Patents.e  cit-Patents  4 10 1e-4
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
   Directed edge-list loader
   Format: "src dst\n"  — edges stored as-is (no reverse added)
   --------------------------------------------------------------- */
static PageRankGraph *load_directed_edgelist(const char *filename) {
    printf("[Loader] Opening %s ...\n", filename);
    fflush(stdout);

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[Loader] ERROR: cannot open %s\n", filename);
        return NULL;
    }

    /* --- Pass 1: find max node id and count edges --- */
    long edge_count = 0;
    int  max_id     = -1;
    int  u, v;

    while (fscanf(f, "%d %d", &u, &v) == 2) {
        if (u > max_id) max_id = u;
        if (v > max_id) max_id = v;
        edge_count++;
    }

    int N = max_id + 1;
    printf("[Loader] Pass 1 done: %d nodes, %ld directed edges\n", N, edge_count);
    fflush(stdout);

    /* --- Allocate out-degree array --- */
    int *out_deg = calloc(N, sizeof(int));
    if (!out_deg) { fclose(f); fprintf(stderr,"[Loader] OOM\n"); return NULL; }

    /* --- Pass 2: count per-node out-degree --- */
    rewind(f);
    while (fscanf(f, "%d %d", &u, &v) == 2) {
        out_deg[u]++;   /* directed: only u -> v */
    }

    /* --- Allocate adjacency arrays --- */
    int **outlinks = malloc(N * sizeof(int *));
    if (!outlinks) { free(out_deg); fclose(f); fprintf(stderr,"[Loader] OOM\n"); return NULL; }

    for (int i = 0; i < N; i++) {
        outlinks[i] = (out_deg[i] > 0) ? malloc(out_deg[i] * sizeof(int)) : NULL;
    }

    int *fill = calloc(N, sizeof(int));
    if (!fill) {
        for (int i = 0; i < N; i++) free(outlinks[i]);
        free(outlinks); free(out_deg); fclose(f);
        fprintf(stderr,"[Loader] OOM\n"); return NULL;
    }

    /* --- Pass 3: fill adjacency lists --- */
    rewind(f);
    while (fscanf(f, "%d %d", &u, &v) == 2) {
        outlinks[u][fill[u]++] = v;
    }
    fclose(f);
    free(fill);

    printf("[Loader] Graph ready.\n\n");
    fflush(stdout);

    /* --- Build PageRankGraph struct --- */
    PageRankGraph *pg = malloc(sizeof(PageRankGraph));
    if (!pg) {
        for (int i = 0; i < N; i++) free(outlinks[i]);
        free(outlinks); free(out_deg);
        fprintf(stderr,"[Loader] OOM\n"); return NULL;
    }

    pg->num_nodes      = N;
    pg->outlink_counts = out_deg;
    pg->outlinks       = outlinks;
    pg->ranks          = malloc(N * sizeof(double));
    pg->new_ranks      = malloc(N * sizeof(double));

    if (!pg->ranks || !pg->new_ranks) {
        fprintf(stderr,"[Loader] OOM allocating rank arrays\n");
        pagerank_free(pg);
        return NULL;
    }

    return pg;
}

/* ---------------------------------------------------------------
   Main
   --------------------------------------------------------------- */
int main(int argc, char **argv) {

    if (argc < 3) {
        printf("Usage: %s <edge_file> <dataset_name> [threads] [max_iters] [threshold]\n", argv[0]);
        printf("  e.g. %s wiki-Talk/wiki-Talk.e wiki-Talk 4 10 1e-4\n", argv[0]);
        printf("  e.g. %s citi-patents/cit-Patents.e cit-Patents 4 10 1e-4\n", argv[0]);
        return 1;
    }

    const char *edge_file    = argv[1];
    const char *dataset_name = argv[2];
    int         threads      = (argc > 3) ? atoi(argv[3]) : 4;
    int         max_iters    = (argc > 4) ? atoi(argv[4]) : 10;
    double      threshold    = (argc > 5) ? atof(argv[5]) : 1e-4;

    if (threads < 1) threads = 1;

    /* ---- Load dataset ---- */
    double t0 = wall_ms();
    PageRankGraph *pg = load_directed_edgelist(edge_file);
    if (!pg) return 1;
    double load_ms = wall_ms() - t0;

    long total_edges = 0;
    for (int i = 0; i < pg->num_nodes; i++) total_edges += pg->outlink_counts[i];

    printf("[Benchmark] %s loaded in %.2f ms\n", dataset_name, load_ms);
    printf("============================================================\n");
    printf("  BENCHMARK: %s\n", dataset_name);
    printf("  Nodes: %d  |  Edges: %ld\n", pg->num_nodes, total_edges);
    printf("  Threads: %d  |  Max iters: %d  |  Threshold: %.0e\n",
           threads, max_iters, threshold);
    printf("  Damping: %.2f\n", DAMPING_FACTOR);
    printf("============================================================\n\n");

    /* ---- Output filename based on dataset name ---- */
    char outfile[256];
    snprintf(outfile, sizeof(outfile), "%s_strategy_results.txt", dataset_name);

    /* ---- Run all four strategies ---- */
    pagerank_run_comparison(pg, threads, max_iters, threshold, outfile);

    /* ---- Thread scalability sweep (Distributed + Convergence) ---- */
    printf("\n============================================================\n");
    printf("  THREAD SCALABILITY SWEEP — %s\n", dataset_name);
    printf("============================================================\n");
    printf("%-16s | %10s | %7s | %10s\n",
           "Threads", "Time (ms)", "Speedup", "Efficiency");
    printf("-----------------+------------+---------+------------\n");

    int    thread_counts[] = {1, 2, 4, 8};
    double base_time       = 0.0;

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

    /* ---- Communication overhead ---- */
    printf("\n============================================================\n");
    printf("  COMMUNICATION OVERHEAD — %s\n", dataset_name);
    printf("============================================================\n");

    double data_mb = (double)pg->num_nodes * sizeof(double) / (1024.0 * 1024.0);

    if (pg->num_nodes > 500000) {
        /* Centralized mutex strategy skipped for large graphs (N > 500K).
           Per-node mutexes cause O(N) acquisitions per dangling node — impractical at scale.
           Overhead is reported via memory layout only. */
        printf("  NOTE: Centralized strategy NOT run (N=%d > 500K nodes).\n", pg->num_nodes);
        printf("  See crawled_graph results for centralized vs distributed timing comparison.\n\n");

        StrategyResult distrib = pagerank_distributed_convergence(pg, threads, threshold);
        printf("  Distributed time : %.2f ms\n", distrib.elapsed_ms);
        printf("  ranks[] memory   : %.2f MB\n", data_mb);
        printf("  Thread buffers(%dx): %.2f MB\n", threads, threads * data_mb);
        printf("  Mutex overhead   : N/A (centralized not run at this scale)\n");
    } else {
        StrategyResult central = pagerank_centralized_convergence(pg, threads, threshold);
        StrategyResult distrib = pagerank_distributed_convergence(pg, threads, threshold);

        double overhead_pct = 0.0;
        if (distrib.elapsed_ms > 0.0)
            overhead_pct = ((central.elapsed_ms - distrib.elapsed_ms) / distrib.elapsed_ms) * 100.0;

        printf("  Centralized time : %.2f ms\n", central.elapsed_ms);
        printf("  Distributed time : %.2f ms\n", distrib.elapsed_ms);
        printf("  Mutex overhead   : %.1f%%\n", overhead_pct > 0 ? overhead_pct : 0.0);
        printf("  ranks[] memory   : %.2f MB\n", data_mb);
        printf("  Thread buffers(%dx): %.2f MB\n", threads, threads * data_mb);
    }
    printf("============================================================\n\n");

    pagerank_free(pg);
    printf("[Benchmark] Done. Results saved to %s\n", outfile);
    return 0;
}
