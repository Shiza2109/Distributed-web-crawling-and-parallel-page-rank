# Distributed Web Crawling and Parallel PageRank

This project implements a C-based web crawling pipeline with support for:

- single-thread and multi-thread crawling
- deterministic/manual crawling workflows for reproducibility
- graph generation from crawled pages for downstream ranking analysis

The codebase is organized around modular crawler components (frontier, fetcher, parser, graph) and multiple worker modes.

## What This Project Does

1. Starts from configured seed URLs.
2. Fetches page content and parses out links.
3. Applies crawl policies to decide whether URLs should be visited.
4. Deduplicates and schedules URLs in a frontier.
5. Builds crawl graph artifacts (`.adj` and `.edge`) for analysis.
6. Supports threaded and manual workflows for comparison and reproducibility.

## Project Structure

### Core Crawler Modules

- `fetch.c` / `fetch.h`: HTTP retrieval logic for page content.
- `parser.c` / `parser.h`: link extraction/parsing utilities.
- `crawl_policy.c` / `crawl_policy.h`: URL filtering and crawl constraints.
- `frontier.c` / `frontier.h`: URL frontier queue/set management.
- `graph.c` / `graph.h`: graph construction and export (`.adj`, `.edge`).
- `worker.c` / `worker.h`: threaded/standard worker execution.
- `manual_worker.c` / `manual_worker.h`: manual deterministic worker flow.
- `reproducibility.c` / `reproducibility.h`: reproducibility helpers and controls.

### Integration and Tests

- `test_integration.c`: end-to-end integration runner (modes, thread counts, profiles).
- `test_duplicate.c`: duplicate URL handling tests.
- `test_ibrahim.c`: additional crawler behavior checks.

### Build and Configuration

- `Makefile`: build targets.
- `manifest.txt`: run/profile manifest used by experiments.

### Generated / Data Artifacts

- `crawled_graph.adj`, `crawled_graph.edge`: graph output.
- `crawled_graph_threaded.adj`, `crawled_graph_threaded.edge`: threaded graph output.
- `url_map.txt`, `url_map_manual.txt`, `url_map_threaded.txt`: URL-index mappings.
- `test_graph_adj.txt`, `test_graph_edge.txt`, `test_url_map.txt`: test reference artifacts.

## Build

Use MinGW/GCC (or equivalent C toolchain) from project root:

```bash
make
```

## Running Integration Scenarios

Example:

```bash
./test_integration.exe --mode threaded --profile small --threads 4 --depth 1 --max-pages 50
```

Supported modes used in current workflow:

- `compare`: compare behavior/performance between crawling strategies.
- `threaded`: parallel worker pipeline.
- `manual`: deterministic manual worker pipeline.

Useful parameters:

- `--profile`: predefined crawl profile (for example, `small`, `large`).
- `--threads`: thread count for non-manual modes.
- `--depth`: crawl depth limit.
- `--max-pages`: page cap for bounded runs.

## Typical Experiment Sweep (PowerShell)

```powershell
$modes = "compare","threaded","manual"
$profiles = "small","large"
$threads = 1,2,4,8
$depths = 0,1
$pages = 50,500

foreach ($m in $modes) {
  foreach ($p in $profiles) {
    foreach ($d in $depths) {
      foreach ($mp in $pages) {
        if ($m -eq "manual") {
          .\test_integration.exe --mode $m --profile $p --threads 1 --depth $d --max-pages $mp
        } else {
          foreach ($t in $threads) {
            .\test_integration.exe --mode $m --profile $p --threads $t --depth $d --max-pages $mp
          }
        }
      }
    }
  }
}
```

## Notes on Reproducibility

- Use `manual` mode for deterministic control flow.
- Keep seed input/profile configuration fixed across benchmark runs.
- Compare generated graph and URL map artifacts between modes.

## Repository Branch Context

- `main`: intended stable/default branch.
- `m1_mibrahim`: milestone branch containing crawler updates and artifacts.
- `m1_shiza`: reduced branch variant containing selected worker/fetch files.

## License

No explicit license file is currently included in this repository.
