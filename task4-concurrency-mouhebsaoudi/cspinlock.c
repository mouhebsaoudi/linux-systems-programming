#include <stdatomic.h>
#include <stdlib.h>
#include "cspinlock.h"

struct cspinlock {
    atomic_flag flag;
};

int cspin_lock(cspinlock_t *slock) {
    if (!slock) return -1;
    while (atomic_flag_test_and_set(&slock->flag)) {
    }
    return 0;
}

int cspin_trylock(cspinlock_t *slok) {
    if (!slok) return -1;
    if (atomic_flag_test_and_set(&slok->flag)) return -1;
    return 0;
}

int cspin_unlock(cspinlock_t *slock){
    if (!slock) return -1;
    atomic_flag_clear(&slock->flag);
    return 0;
}

cspinlock_t *cspin_alloc() {
    cspinlock_t *lock =malloc(sizeof(cspinlock_t));
    if (!lock) return NULL;
    atomic_flag_clear(&lock->flag);
    return lock;
}

void cspin_free(cspinlock_t *slock) {
    if (!slock) return;
    free(slock);
}





