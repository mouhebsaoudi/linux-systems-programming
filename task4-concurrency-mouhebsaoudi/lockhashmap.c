#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "chashmap.h"

typedef struct {
    atomic_flag flag;
} spinlock_t;

static void spinlock_init(spinlock_t *l) {
    atomic_flag_clear(&l->flag);
}

static void spinlock_lock(spinlock_t *l){
    while (atomic_flag_test_and_set(&l->flag)) {
    }
}

static void spinlock_unlock(spinlock_t *l) {
    atomic_flag_clear(&l->flag);
}

struct Node_HM_t{
    long m_val;

    char padding[PAD];
    struct Node_HM_t *m_next;
};

struct List_t {
    struct Node_HM_t *head;
    spinlock_t lock;
};

struct hm_t{
    size_t n_buckets;
    struct List_t *buckets;
};

static size_t hash_value(HM *hm, long val){
    unsigned long x = (unsigned long)val;
    if (hm->n_buckets== 0) return 0;

    return x % hm->n_buckets;
}

HM *alloc_hashmap(size_t n_buckets) {
    if (n_buckets ==0) n_buckets= 1;
    HM *hm = malloc(sizeof(HM));
    if (!hm) return NULL;
    hm->n_buckets =n_buckets;
    hm->buckets =malloc(n_buckets * sizeof(struct List_t));
    if (!hm->buckets) {
        free(hm);
        return NULL;
    }
    for (size_t i = 0; i < n_buckets; i++) {
        spinlock_init(&hm->buckets[i].lock);
        hm->buckets[i].head = NULL;
    }
    return hm;
}

void free_hashmap(HM *hm){
    if (!hm) return;
    if (hm->buckets) {
        for (size_t i = 0; i < hm->n_buckets; i++) {
            struct List_t *list = &hm->buckets[i];
            struct Node_HM_t *cur = list->head;
            while (cur){
                struct Node_HM_t *next = cur->m_next;
                free(cur);
                cur = next;
            }
        }
        free(hm->buckets);
    }
    free(hm);
}

int insert_item(HM *hm, long val){
    if (!hm) return 1;
    size_t idx = hash_value(hm, val);
    if (idx >= hm->n_buckets) return 1;
    struct List_t *list = &hm->buckets[idx];
    spinlock_lock(&list->lock);
    struct Node_HM_t *node = malloc(sizeof(struct Node_HM_t));
    if (!node) {
        spinlock_unlock(&list->lock);
        return 1;
    }
    node->m_val =val;
    node->m_next= list->head;
    list->head = node;
    spinlock_unlock(&list->lock );
    return 0;
}

int remove_item(HM *hm, long val) {
    if (!hm) return 1;
    size_t idx = hash_value(hm,val);
    if (idx >= hm->n_buckets) return 1;
    struct List_t *list = &hm->buckets[idx];
    spinlock_lock(&list->lock);
    struct Node_HM_t *prev = NULL;
    struct Node_HM_t *cur = list->head;
    while (cur) {
        if (cur->m_val == val) {
            if (prev) {
                prev->m_next = cur->m_next;
            } else {
                list->head = cur->m_next;
            }
            free(cur);
            spinlock_unlock(&list->lock);
            return 0;
        }
        prev= cur;
        cur =cur->m_next;
    }
    spinlock_unlock(&list->lock) ;
    return 1;
}

int lookup_item(HM *hm,long val) {
    if (!hm) return 1;
    size_t idx = hash_value(hm, val);
    if (idx >= hm->n_buckets) return 1;
    struct List_t *list = &hm->buckets[idx];
    spinlock_lock(&list->lock);
    struct Node_HM_t *cur = list->head;
    while (cur){
        if (cur->m_val == val) {
            spinlock_unlock(&list->lock);
            return 0;
        }
        cur= cur->m_next;
    }
    spinlock_unlock(&list->lock);
    return 1;
}

void print_hashmap(HM *hm){
    if (!hm) return;
    for (size_t i = 0; i < hm->n_buckets; i++) {
        struct List_t *list = &hm->buckets[i];
        spinlock_lock(&list->lock);
        printf("Bucket %zu", i + 1);
        struct Node_HM_t *cur = list->head;
        while (cur){
            printf(" - %ld", cur->m_val);
            cur = cur->m_next;
        }
        printf("\n");
        spinlock_unlock(&list->lock);
    }
}
