/* worker.h */
#ifndef WORKER_H
#define WORKER_H

#include "frontier.h"
#include "graph.h"

#define WORKER_MAX_THREADS  32

typedef struct {
    int       worker_id;
    Frontier *frontier;
    Graph    *graph;            /* shared graph — graph.c handles its own lock */
} WorkerArgs;

typedef struct {
    pthread_t  threads[WORKER_MAX_THREADS];
    WorkerArgs args[WORKER_MAX_THREADS];
    int        n_threads;
} WorkerPool;

/*
 * worker_pool_start — spawn n_threads workers, all sharing frontier+graph.
 * Returns 0 on success.
 */
int  worker_pool_start(WorkerPool *pool, Frontier *frontier,
                        Graph *graph, int n_threads);

/*
 * worker_pool_join — block until all workers exit.
 */
void worker_pool_join(WorkerPool *pool);

#endif