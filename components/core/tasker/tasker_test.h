#ifndef TASKER_TEST_H
#define TASKER_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the full tasker test suite (basic functional + stress)
 * 
 * Must be called after tasker_init(). The suite will submit tasks to the
 * scheduler, wait for them to process, and print a summary report to stdout.
 * 
 * Expected runtime: ~20-30 seconds on ESP32-S3.
 */
void tasker_test_all(void);

/**
 * @brief Run only the basic functional tests (15 cases)
 * 
 * Covers: null params, cancel, priority, periodic, timeout, sched full, fail
 * handling, level upgrade, and basic multi-task concurrency.
 * 
 * Expected runtime: ~3-5 seconds.
 */
void tasker_test_basic(void);

/**
 * @brief Run only the stress tests (10 cases)
 * 
 * Covers: sched full flood, periodic tsunami, rapid fire one-shots, mixed
 * level marathon, cancel race, timeout cascade, memory churn, CPU saturation,
 * cancel+re-enqueue, and endurance run.
 * 
 * Expected runtime: ~15-20 seconds.
 */
void tasker_test_stress(void);

#ifdef __cplusplus
}
#endif

#endif // TASKER_TEST_H
