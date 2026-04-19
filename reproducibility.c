#include "reproducibility.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

uint64_t manifest_compute_hash(const char **urls, int count) {
    const uint64_t fnv_offset = 1469598103934665603ULL;
    const uint64_t fnv_prime = 1099511628211ULL;
    uint64_t hash = fnv_offset;

    if (!urls || count <= 0) return hash;

    for (int i = 0; i < count; i++) {
        const char *u = urls[i];
        if (!u) continue;
        while (*u) {
            hash ^= (unsigned char)(*u++);
            hash *= fnv_prime;
        }
        /* Delimiter so ["ab","c"] differs from ["a","bc"]. */
        hash ^= (unsigned char)'\n';
        hash *= fnv_prime;
    }

    return hash;
}

void manifest_save(CrawlManifest *m, const char *filename) {
    if (!m || !filename) return;

    time_t now = time(NULL);
    struct tm tm_info;

#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif

    strftime(m->timestamp, sizeof(m->timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    FILE *f = fopen(filename, "a");
    if (!f) {
        fprintf(stderr, "[Manifest] Failed to open %s for writing\n", filename);
        return;
    }

    fprintf(f, "# Crawl Manifest\n");
    fprintf(f, "timestamp=%s\n", m->timestamp);
    fprintf(f, "num_seeds=%d\n", m->num_seeds);
    fprintf(f, "max_depth=%d\n", m->max_depth);
    fprintf(f, "max_pages=%" PRIu64 "\n", m->max_pages);
    fprintf(f, "same_domain_only=%d\n", m->same_domain_only);
    fprintf(f, "num_workers=%d\n", m->num_workers);
    fprintf(f, "total_nodes=%" PRIu64 "\n", m->total_nodes);
    fprintf(f, "total_edges=%" PRIu64 "\n", m->total_edges);
    fprintf(f, "seeds_added=%d\n", m->seeds_added);
    fprintf(f, "elapsed_ms=%.2f\n", m->elapsed_ms);
    fprintf(f, "pages_discovered=%" PRIu64 "\n", m->pages_discovered);
    fprintf(f, "pages_fetched=%" PRIu64 "\n", m->pages_fetched);
    fprintf(f, "duplicates_filtered=%" PRIu64 "\n", m->duplicates_filtered);
    fprintf(f, "content_hash=%" PRIu64 "\n", m->content_hash);
    fprintf(f, "graph_file=%s\n", m->graph_file);
    fprintf(f, "url_map_file=%s\n", m->url_map_file);
    fprintf(f, "log_file=%s\n", m->log_file);

    fclose(f);
}
