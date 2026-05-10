/*
 * m3_integrated_pipeline.c — Milestone 3 Complete Pipeline
 *
 * Integrated workflow:
 *   1. Phase 1: Load pre-generated graph (Aleena — m3_incremental_webcrawler)
 *   2. Phase 2: Web crawling with new seeds
 *   3. Phase 2: Strategy comparison — Static vs Incremental PageRank (Shiza)
 *
 * Single executable that orchestrates the complete pipeline.
 * Supports both web crawling and pre-generated graphs (graph500).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

/* Forward declarations for external functions */

/* From m3_incremental_pagerank.c */
typedef struct {
    int num_nodes_old;
    int num_nodes_new;
    int num_new_nodes;
    int num_affected_nodes;
    uint64_t iterations;
    double elapsed_ms;
    double convergence_delta;
} RecomputationStats;

typedef struct {
    RecomputationStats *full;
    RecomputationStats *incremental;
    double speedup;
    double avg_rank_diff;
    double max_rank_diff;
} ComparisonResult;

extern ComparisonResult* incremental_compare_strategies_files(
    const char *phase1_graph_file,
    const char *phase2_graph_file,
    int num_threads
);
extern void incremental_free_comparison(ComparisonResult *result);

/* ============================================================
   Main Integrated Pipeline
   ============================================================ */

int main(int argc, char **argv) {
    int crawler_threads = 4;
    int pagerank_threads = 4;
    int max_depth = 1;
    uint64_t max_pages = 100;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--crawler-threads") == 0 && i + 1 < argc) {
            crawler_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pagerank-threads") == 0 && i + 1 < argc) {
            pagerank_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            max_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc) {
            max_pages = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Milestone 3: Incremental Web Crawling and PageRank\n");
            printf("\nUsage: %s [options]\n\n", argv[0]);
            printf("Crawler Options:\n");
            printf("  --crawler-threads N      Threads for web crawler (default: 4)\n");
            printf("  --depth N                Crawl depth (default: 1)\n");
            printf("  --max-pages N            Maximum pages to fetch (default: 100)\n");
            printf("\nPageRank Options:\n");
            printf("  --pagerank-threads N     Threads for PageRank (default: 4)\n");
            printf("\nOther Options:\n");
            printf("  -h, --help               Show this message\n");
            return 0;
        }
    }

    printf("\n");
    printf("========================================================\n");
    printf("  MILESTONE 3: Incremental Web Crawling & PageRank Pipeline\n");
    printf("  Load Phase1 -> Crawl Phase2 -> Compare Strategies\n");
    printf("========================================================\n\n");

    printf("Configuration:\n");
    printf("  Crawler threads:   %d\n", crawler_threads);
    printf("  PageRank threads:  %d\n", pagerank_threads);
    printf("  Crawl depth:       %d\n", max_depth);
    printf("  Max pages:         %" PRIu64 "\n\n", max_pages);

    /* STEP 1: Load Phase 1 & Crawl Phase 2 */
    printf("========================================================\n");
    printf("  STEP 1: Load Phase 1 Graph + Crawl Phase 2\n");
    printf("  (Aleena - m3_incremental_webcrawler)\n");
    printf("========================================================\n\n");

    printf("Running: ./m3_incremental_webcrawler.exe --threads %d --depth %d --max-pages %" PRIu64 "\n\n",
           crawler_threads, max_depth, max_pages);

    /* Build command */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), ".\\m3_incremental_webcrawler.exe --threads %d --depth %d --max-pages %" PRIu64,
             crawler_threads, max_depth, max_pages);

    int crawler_result = system(cmd);
    if (crawler_result != 0) {
        fprintf(stderr, "\nERROR: Crawler execution failed (exit code: %d)\n", crawler_result);
        fprintf(stderr, "Make sure m3_incremental_webcrawler.exe is compiled and in the current directory.\n");
        fprintf(stderr, "Also ensure phase1_graph.adj exists. Use graph500_to_phase1_converter.exe to generate it.\n");
        return 1;
    }

    /* STEP 2: Load Graphs for Strategy Comparison */
    printf("\n========================================================\n");
    printf("  STEP 2: Load Graphs for Incremental PageRank Analysis\n");
    printf("  (Shiza - m3_incremental_pagerank + m3_incremental_webcrawler)\n");
    printf("========================================================\n\n");

    /* Verify files exist before comparison */
    printf("Verifying output files...\n");
    FILE *p1_check = fopen("phase1_graph.adj", "r");
    FILE *p2_check = fopen("phase2_graph.adj", "r");
    FILE *ranks_check = fopen("phase1_ranks.txt", "r");

    if (!p1_check || !p2_check || !ranks_check) {
        fprintf(stderr, "ERROR: Missing required files:\n");
        if (!p1_check) fprintf(stderr, "  - phase1_graph.adj\n");
        if (!p2_check) fprintf(stderr, "  - phase2_graph.adj\n");
        if (!ranks_check) fprintf(stderr, "  - phase1_ranks.txt\n");
        fprintf(stderr, "Ensure m3_incremental_webcrawler completed successfully.\n");
        return 1;
    }

    if (p1_check) fclose(p1_check);
    if (p2_check) fclose(p2_check);
    if (ranks_check) fclose(ranks_check);

    printf("[OK] All required files found\n");

    /* STEP 3: Run strategy comparison */
    printf("\n========================================================\n");
    printf("  STEP 3: Compare Recomputation Strategies\n");
    printf("  Static (full) vs Incremental (warm-start)\n");
    printf("========================================================\n\n");

    ComparisonResult *result = incremental_compare_strategies_files(
        "phase1_graph.adj",
        "phase2_graph.adj",
        pagerank_threads
    );
    if (!result) {
        fprintf(stderr, "ERROR: Strategy comparison failed\n");
        return 1;
    }

    /* STEP 4: Print summary */
    printf("\n========================================================\n");
    printf("  FINAL SUMMARY\n");
    printf("========================================================\n\n");

    printf("Pipeline Results:\n");
    printf("  Phase 1 Graph:\n");
    printf("    - Nodes: %d\n", result->full->num_nodes_old);
    printf("\n");

    printf("  Phase 2 Graph:\n");
    printf("    - Nodes: %d (+%d new)\n", result->full->num_nodes_new, result->full->num_new_nodes);
    printf("\n");

    printf("  PageRank Strategy Comparison:\n");
    printf("    - Static time:      %.1f ms\n", result->full->elapsed_ms);
    printf("    - Incremental time: %.1f ms\n", result->incremental->elapsed_ms);
    printf("    - Speedup:          %.2fx\n", result->speedup);
    printf("    - Avg rank diff:    %.6e\n", result->avg_rank_diff);
    printf("    - Max rank diff:    %.6e\n", result->max_rank_diff);
    printf("\n");

    if (result->speedup > 1.0) {
        printf("  RECOMMENDATION: Incremental strategy is %.2fx faster\n", result->speedup);
    } else if (result->speedup < 1.0) {
        printf("  RECOMMENDATION: Static strategy is %.2fx faster\n", 1.0 / result->speedup);
    } else {
        printf("  RECOMMENDATION: Both strategies have similar performance\n");
    }

    printf("\n  Output Files Generated:\n");
    printf("    Phase 1:\n");
    printf("      - phase1_graph.adj / .edge\n");
    printf("      - phase1_ranks.txt\n");
    printf("      - phase1_url_map.txt\n");
    printf("\n");
    printf("    Phase 2:\n");
    printf("      - phase2_graph.adj / .edge\n");
    printf("      - phase2_url_map.txt\n");
    printf("      - phase2_ranks_full.txt (static recomputation)\n");
    printf("      - phase2_ranks_incremental.txt (incremental warm-start)\n");
    printf("\n");

    /* Cleanup */
    incremental_free_comparison(result);

    printf("========================================================\n");
    printf("  MILESTONE 3 PIPELINE COMPLETE\n");
    printf("========================================================\n\n");;

    return 0;
}
