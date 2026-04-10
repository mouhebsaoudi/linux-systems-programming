#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include "chashmap.h"

struct Node_HM_t {
    long m_val;
    char padding[PAD];
    struct Node_HM_t *m_next;
    _Atomic(int) deleted;
};

struct List_t {
    _Atomic(struct Node_HM_t *) head;
};

struct hm_t {
    size_t n_buckets;
    struct List_t *buckets;
};

static size_t hash_value(HM *hm, long val) {
    unsigned long x = (unsigned long)val;
    if (hm->n_buckets == 0) return 0;
    return x % hm->n_buckets;
}

HM *alloc_hashmap(size_t n_buckets) {
    if (n_buckets == 0) n_buckets = 1;
    HM *hm = malloc(sizeof(HM));
    if (!hm) return NULL;
    hm->n_buckets = n_buckets;
    hm->buckets = malloc(n_buckets * sizeof(struct List_t));
    if (!hm->buckets){
        free(hm);
        return NULL;
    }
    for (size_t i = 0; i < n_buckets; i++){
        atomic_store_explicit(&hm->buckets[i].head, NULL, memory_order_relaxed);
    }

    return hm;
}

void free_hashmap(HM *hm){
    if (!hm) return;

    if (hm->buckets ) {
        for (size_t i = 0; i < hm->n_buckets; i++) {
            struct Node_HM_t *cur = atomic_load_explicit(&hm->buckets[i].head, memory_order_acquire);
            while (cur) {
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

    struct Node_HM_t *node = malloc(sizeof(struct Node_HM_t ));
    if (!node) return 1;
    node->m_val =val;
    node->m_next= NULL;
    atomic_store_explicit(&node->deleted, 0, memory_order_relaxed);
    for (;;){
        struct Node_HM_t *head = atomic_load_explicit(&list->head, memory_order_acquire);
        node->m_next = head;
        if (atomic_compare_exchange_weak_explicit(&list->head, &head, node,
                                                  memory_order_release, memory_order_relaxed)) {
            return 0;
        }
    }
}

int remove_item(HM *hm, long val) {
    if (!hm) return 1;
    size_t idx = hash_value(hm, val);
    if (idx >= hm->n_buckets) return 1;
    struct List_t *list = &hm->buckets[idx];
    int found = 0;
    struct Node_HM_t *cur = atomic_load_explicit(&list->head, memory_order_acquire);
    while (cur) {
        if (cur->m_val== val) {
            found =1;
            int expected= 0;
            atomic_compare_exchange_strong_explicit(&cur->deleted, &expected, 1,
                                                        memory_order_acq_rel, memory_order_relaxed) ;
            
            
        }
        cur = cur->m_next;
    }
    return found ? 0 : 1;
}

int lookup_item(HM *hm, long val) {
    if (!hm) return 1;
    size_t idx =hash_value(hm, val);
    if (idx >= hm->n_buckets) return 1;
    struct List_t *list = &hm->buckets[idx];
    struct Node_HM_t *cur = atomic_load_explicit(&list->head, memory_order_acquire);
    while (cur) {
        int del = atomic_load_explicit(&cur->deleted, memory_order_acquire);
        if (!del && cur->m_val == val) return 0;
        cur =cur->m_next;
    }
    return 1;
}

void print_hashmap(HM *hm) {
    if (!hm) return;
    for (size_t i = 0; i < hm->n_buckets; i++) {
        struct List_t *list= &hm->buckets[i];
        printf("Bucket %zu", i + 1);
        struct Node_HM_t *cur= atomic_load_explicit(&list->head, memory_order_acquire);
        while (cur) {
            int del = atomic_load_explicit(&cur->deleted, memory_order_acquire);
            if (!del) {
                printf(" - %ld", cur->m_val);
            }
            cur = cur->m_next;
        }
        printf("\n");
    }
}
