#ifndef MANUAL_WORKER_H
#define MANUAL_WORKER_H

#include "frontier.h"
#include "graph.h"

/*
 * run_manual_crawl - single-thread crawler loop for benchmarking against worker pool.
 * Returns 0 on success.
 */
int run_manual_crawl(Frontier *frontier, Graph *graph);

#endif