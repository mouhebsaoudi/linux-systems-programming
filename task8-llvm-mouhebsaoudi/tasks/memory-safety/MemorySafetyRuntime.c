#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SHADOW_SCALE 3
#define SHADOW_GRANULE (1ULL << SHADOW_SCALE)

#define SHADOW_PAGE_SHIFT 12
#define SHADOW_PAGE_SIZE (1U << SHADOW_PAGE_SHIFT)

#define L1_BITS 11
#define L2_BITS 11
#define L3_BITS 11

#define L1_SIZE (1U << L1_BITS)
#define L2_SIZE (1U << L2_BITS)
#define L3_SIZE (1U << L3_BITS)

#define REDZONE_SIZE 32

#define POISON_HEAP_REDZONE ((uint8_t)0xF1)
#define POISON_STACK_REDZONE ((uint8_t)0xF2)
#define POISON_FREED ((uint8_t)0xF3)

typedef uint8_t *ShadowPage;
typedef ShadowPage *L3Table;
typedef L3Table *L2Table;

static L2Table g_l1[L1_SIZE];

static __thread uint64_t tls_cached_page_num = UINT64_MAX;
static __thread uint8_t *tls_cached_page = NULL;

static inline void die_illegal(void) {
    fprintf(stderr, "Illegal memory access\n");
    exit(1);
}

static inline uint8_t *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return (uint8_t *)p;
}

static inline ShadowPage get_shadow_page(uint64_t page_num, int create) {
    uint64_t l1 = page_num >> (L2_BITS + L3_BITS);
    uint64_t l2 = (page_num >> L3_BITS) & (uint64_t)(L2_SIZE - 1);
    uint64_t l3 = page_num & (uint64_t)(L3_SIZE - 1);

    if (l1 >= L1_SIZE) return NULL;

    L2Table l2tab = g_l1[l1];
    if (!l2tab) {
        if (!create) return NULL;
        l2tab = (L2Table)xcalloc(L2_SIZE, sizeof(L3Table));
        g_l1[l1] = l2tab;
    }

    L3Table l3tab = l2tab[l2];
    if (!l3tab) {
        if (!create) return NULL;
        l3tab = (L3Table)xcalloc(L3_SIZE, sizeof(ShadowPage));
        l2tab[l2] = l3tab;
    }

    ShadowPage page = l3tab[l3];
    if (!page) {
        if (!create) return NULL;
        page = (ShadowPage)xcalloc(SHADOW_PAGE_SIZE, 1);
        l3tab[l3] = page;
    }

    return page;
}

static inline int8_t shadow_load(uint64_t shadow_index) {
    uint64_t page_num = shadow_index >> SHADOW_PAGE_SHIFT;
    uint32_t off = (uint32_t)(shadow_index & (SHADOW_PAGE_SIZE - 1));

    if (page_num == tls_cached_page_num) {
        if (!tls_cached_page) return 0;
        return (int8_t)tls_cached_page[off];
    }

    uint8_t *page = get_shadow_page(page_num, 0);
    tls_cached_page_num = page_num;
    tls_cached_page = page;
    if (!page) return 0;
    return (int8_t)page[off];
}

static inline void shadow_store(uint64_t shadow_index, uint8_t val) {
    uint64_t page_num = shadow_index >> SHADOW_PAGE_SHIFT;
    uint32_t off = (uint32_t)(shadow_index & (SHADOW_PAGE_SIZE - 1));

    uint8_t *page = get_shadow_page(page_num, 1);
    page[off] = val;

    if (page_num == tls_cached_page_num) tls_cached_page = page;
}

static inline void shadow_fill(uint64_t shadow_start, uint64_t shadow_end, uint8_t val) {
    while (shadow_start < shadow_end) {
        uint64_t page_num = shadow_start >> SHADOW_PAGE_SHIFT;
        uint32_t off = (uint32_t)(shadow_start & (SHADOW_PAGE_SIZE - 1));

        uint64_t avail = (uint64_t)SHADOW_PAGE_SIZE - (uint64_t)off;
        uint64_t remain = shadow_end - shadow_start;
        uint64_t n = (avail < remain) ? avail : remain;

        uint8_t *page = get_shadow_page(page_num, val != 0);
        if (page) memset(page + off, val, (size_t)n);

        if (page_num == tls_cached_page_num) tls_cached_page = page;
        shadow_start += n;
    }
}

static inline void unpoison_range(uintptr_t addr, uint64_t size) {
    uint64_t start = ((uint64_t)addr) >> SHADOW_SCALE;
    uint64_t end = ((uint64_t)addr + size) >> SHADOW_SCALE;

    shadow_fill(start, end, 0);

    uint64_t rem = ((uint64_t)addr + size) & (SHADOW_GRANULE - 1);
    if (rem != 0) shadow_store(end, (uint8_t)rem);
}

static inline void poison_range(uintptr_t addr, uint64_t size, uint8_t poison) {
    uint64_t start = ((uint64_t)addr) >> SHADOW_SCALE;
    uint64_t end = (((uint64_t)addr + size + (SHADOW_GRANULE - 1)) >> SHADOW_SCALE);
    shadow_fill(start, end, poison);
}

static inline void check_one_chunk(uintptr_t addr, uint64_t size) {
    uint64_t shadow_index = ((uint64_t)addr) >> SHADOW_SCALE;
    int8_t k = shadow_load(shadow_index);

    if (k == 0) return;
    if (k < 0) die_illegal();

    uint64_t off = ((uint64_t)addr) & (SHADOW_GRANULE - 1);
    if (off + size > (uint64_t)k) die_illegal();
}

void __runtime_check_addr(void *addr, uint64_t size) {
    if (!addr || size == 0) die_illegal();

    uintptr_t a = (uintptr_t)addr;
    uint64_t s = size;

    while (s > 0) {
        uint64_t off = ((uint64_t)a) & (SHADOW_GRANULE - 1);
        uint64_t take = SHADOW_GRANULE - off;
        if (take > s) take = s;

        check_one_chunk(a, take);

        a += (uintptr_t)take;
        s -= take;
    }
}

typedef struct {
    uint64_t user_size;
    void *base;
} Header;

static inline uintptr_t align_up(uintptr_t x, uintptr_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

void *__runtime_malloc(uint64_t size) {
    uint64_t total = size + (uint64_t)(2 * REDZONE_SIZE) + (uint64_t)sizeof(Header) + 7ULL;
    uint8_t *base = (uint8_t *)malloc((size_t)total);
    if (!base) return NULL;

    uintptr_t raw = (uintptr_t)base;
    uintptr_t user = align_up(raw + (uintptr_t)sizeof(Header) + (uintptr_t)REDZONE_SIZE, (uintptr_t)SHADOW_GRANULE);

    Header *H = (Header *)(user - (uintptr_t)REDZONE_SIZE);
    H->user_size = size;
    H->base = (void *)base;

    poison_range(user - REDZONE_SIZE, REDZONE_SIZE, POISON_HEAP_REDZONE);
    poison_range(user + (uintptr_t)size, REDZONE_SIZE, POISON_HEAP_REDZONE);
    unpoison_range(user, size);

    return (void *)user;
}

void __runtime_free(void *ptr) {
    if (!ptr) return;

    uintptr_t user = (uintptr_t)ptr;
    Header *H = (Header *)(user - (uintptr_t)REDZONE_SIZE);

    uint64_t size = H->user_size;
    void *base = H->base;

    poison_range(user - REDZONE_SIZE, size + (uint64_t)(2 * REDZONE_SIZE), POISON_FREED);
    free(base);
}

#define STACK_INIT_CAP 1024u
#define STACK_LOAD_NUM 7u
#define STACK_LOAD_DEN 10u

typedef struct {
    uintptr_t key;
    uint64_t size;
} StackEntry;

static __thread StackEntry *stack_table = NULL;
static __thread uint32_t stack_cap = 0;
static __thread uint32_t stack_used = 0;
static __thread uint32_t stack_filled = 0;

static inline void die_oom(void) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
}

static inline uint32_t stack_hash(uintptr_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (uint32_t)x;
}

static inline uint32_t next_pow2_u32(uint32_t x) {
    if (x <= 1) return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

static inline void stack_init_if_needed(void) {
    if (stack_table) return;
    stack_cap = STACK_INIT_CAP;
    stack_table = (StackEntry *)calloc((size_t)stack_cap, sizeof(StackEntry));
    if (!stack_table) die_oom();
    stack_used = 0;
    stack_filled = 0;
}

static inline void stack_rehash(uint32_t new_cap) {
    new_cap = next_pow2_u32(new_cap);
    if (new_cap < STACK_INIT_CAP) new_cap = STACK_INIT_CAP;

    StackEntry *old = stack_table;
    uint32_t old_cap = stack_cap;

    StackEntry *nt = (StackEntry *)calloc((size_t)new_cap, sizeof(StackEntry));
    if (!nt) die_oom();

    stack_table = nt;
    stack_cap = new_cap;
    stack_used = 0;
    stack_filled = 0;

    if (!old) return;

    for (uint32_t i = 0; i < old_cap; ++i) {
        uintptr_t k = old[i].key;
        if (k == 0 || k == (uintptr_t)1) continue;

        uint32_t h = stack_hash(k);
        uint32_t mask = new_cap - 1;

        for (uint32_t step = 0; step < new_cap; ++step) {
            uint32_t idx = (h + step) & mask;
            if (nt[idx].key == 0) {
                nt[idx].key = k;
                nt[idx].size = old[i].size;
                stack_used++;
                stack_filled++;
                break;
            }
        }
    }

    free(old);
}

static inline void stack_maybe_grow(void) {
    stack_init_if_needed();
    if ((uint64_t)(stack_filled + 1) * STACK_LOAD_DEN <= (uint64_t)stack_cap * STACK_LOAD_NUM)
        return;
    stack_rehash(stack_cap * 2u);
}

static inline void stack_put(uintptr_t key, uint64_t size) {
    if (key == 0) return;

    stack_maybe_grow();

    uint32_t h = stack_hash(key);
    uint32_t mask = stack_cap - 1;
    uint32_t first_tomb = UINT32_MAX;

    for (uint32_t step = 0; step < stack_cap; ++step) {
        uint32_t idx = (h + step) & mask;
        uintptr_t k = stack_table[idx].key;

        if (k == key) {
            stack_table[idx].size = size;
            return;
        }

        if (k == (uintptr_t)1) {
            if (first_tomb == UINT32_MAX) first_tomb = idx;
            continue;
        }

        if (k == 0) {
            uint32_t target = (first_tomb != UINT32_MAX) ? first_tomb : idx;
            if (stack_table[target].key == 0) stack_filled++;
            stack_table[target].key = key;
            stack_table[target].size = size;
            stack_used++;
            return;
        }
    }

    stack_rehash(stack_cap * 2u);
    stack_put(key, size);
}

static inline uint64_t stack_take(uintptr_t key) {
    if (!stack_table || key == 0) return 0;

    uint32_t h = stack_hash(key);
    uint32_t mask = stack_cap - 1;

    for (uint32_t step = 0; step < stack_cap; ++step) {
        uint32_t idx = (h + step) & mask;
        uintptr_t k = stack_table[idx].key;

        if (k == key) {
            uint64_t s = stack_table[idx].size;
            stack_table[idx].key = (uintptr_t)1;
            stack_table[idx].size = 0;
            if (stack_used) stack_used--;
            return s;
        }

        if (k == 0) return 0;
    }

    return 0;
}

void __runtime_register_stack(void *ptr, uint64_t size) {
    if (!ptr || size == 0) return;

    uintptr_t p = (uintptr_t)ptr;
    stack_put(p, size);

    poison_range(p - REDZONE_SIZE, REDZONE_SIZE, POISON_STACK_REDZONE);
    poison_range(p + (uintptr_t)size, REDZONE_SIZE, POISON_STACK_REDZONE);
    unpoison_range(p, size);
}

void __runtime_unregister_stack(void *ptr) {
    if (!ptr) return;

    uintptr_t p = (uintptr_t)ptr;
    uint64_t size = stack_take(p);

    if (size == 0) {
        poison_range(p - REDZONE_SIZE, REDZONE_SIZE, 0);
        return;
    }

    poison_range(p - REDZONE_SIZE, REDZONE_SIZE, 0);
    poison_range(p, size, 0);
    poison_range(p + (uintptr_t)size, REDZONE_SIZE, 0);
}
