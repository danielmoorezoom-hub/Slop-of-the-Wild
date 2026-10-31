// Built-in benchmarks (runtime/benchmark.c): a set walk through a place, on a scratch copy of the player's save, with
// the frame rate written down. The game restarts for one and comes back to the start screen after it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int benchmark_count(void);
const char *benchmark_name(int route);
const char *benchmark_about(int route);
const char *benchmark_last(int route);  // the last result, for showing ("" if none yet)
bool benchmark_can_run(void);           // false: no save to start from
void benchmark_run(int route);          // does not return: this process ends and the benchmark's starts

// The benchmark's own process (SAVAGEWIND_BENCHMARK=<route>)
void benchmark_start_up(void);   // (main, once the data root is the working directory) the scratch save, the route, the test settings
bool benchmark_on(void);
void benchmark_frame(uint64_t ns);  // (at every present)
void benchmark_begin(void);      // (the route's `bench begin`, `bench end`)
void benchmark_end(void);
void benchmark_finish(void);     // (the route's `quit`) back to the start screen; returns only in a hidden test run
#ifdef __cplusplus
}
#endif
