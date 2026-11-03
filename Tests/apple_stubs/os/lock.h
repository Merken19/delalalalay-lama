/* Stand-in for <os/lock.h> for Tests/macos_bridge_logic.c (Windows only).
 * The test is single-threaded, so the lock only checks that the bridge
 * never takes it twice (a real os_unfair_lock would crash) or releases it
 * without holding it. */

#ifndef DELAY_LAMA_OS_LOCK_STUB_H
#define DELAY_LAMA_OS_LOCK_STUB_H

#include <stdint.h>

typedef struct os_unfair_lock_s {
    uint32_t held;
} os_unfair_lock;

#define OS_UNFAIR_LOCK_INIT ((os_unfair_lock){0})

/* Set by the stub whenever the lock is misused; checked by the test. */
extern int stub_lock_errors;

static inline void os_unfair_lock_lock(os_unfair_lock *lock) {
    if (lock->held)
        stub_lock_errors++;
    lock->held = 1;
}

static inline void os_unfair_lock_unlock(os_unfair_lock *lock) {
    if (!lock->held)
        stub_lock_errors++;
    lock->held = 0;
}

#endif
