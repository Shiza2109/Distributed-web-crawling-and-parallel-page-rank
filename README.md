# Distributed Web Crawler

A C-based distributed web crawling project with parallel crawling support and PageRank-related graph outputs.

## Build

```bash
make
```

## Run Integration Tests

```bash
./test_integration.exe --mode compare --profile small --threads 4 --depth 1 --max-pages 50
```

## Key Modes

- `compare`: compare crawling behavior/performance modes.
- `threaded`: threaded crawling worker mode.
- `manual`: manual worker path for controlled crawling.