#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "chashmap.h"
#include "timer.h"

const int NUM_ITEMS = 100000; // Items inserted / removed from the hashmap
const int N_BUCKETS = 512;
const int N_THREADS = 4;

volatile int go = 0;
HM *hm;

/* Quick validation of the hashmap */
void validate() {
    int num = rand();

    if (lookup_item(hm, num) == 0) {
        fprintf(stderr, "Hashmap found item %d when it was not present\n", num);
        free_hashmap(hm);
        exit(1);
    }

    if (insert_item(hm, num) != 0) {
        fprintf(stderr, "Hashmap did not insert item %d\n", num);
        free_hashmap(hm);
        exit(1);
    }

    if (lookup_item(hm, num) != 0) {
        fprintf(stderr, "Hashmap did not find item %d when it was present\n", num);
        free_hashmap(hm);
        exit(1);
    }

    if (remove_item(hm, num) != 0) {
        fprintf(stderr, "Hashmap did not remove item %d\n", num);
        free_hashmap(hm);
        exit(1);
    }
}

/* Shuffle array to change order of items during removals compared to insertions */
/* https://benpfaff.org/writings/clc/shuffle.html */
void shuffle_items(int *items) {
    for (size_t i = 0; i < NUM_ITEMS - 1; ++i) {
        size_t j = i + rand() / (RAND_MAX / (NUM_ITEMS - i) + 1);
        int t = items[j];
        items[j] = items[i];
        items[i] = t;
    }
}

/* Fill the hashmap with NUM_ITEMS */
void fill(int *items) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        int num = rand();

        if (insert_item(hm, num) != 0) {
            fprintf(stderr, "Hashmap did not insert item %d\n", num);
            free_hashmap(hm);
            free(items);
            exit(1);
        }

        items[i] = num;
    }

    shuffle_items(items);
}

/* Clear the hashmap */
void clear(int *items) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        int num = items[i];
        if (remove_item(hm, num) != 0) {
            fprintf(stderr, "Hashmap did not remove item %d\n", num);
            free_hashmap(hm);
            free(items);
            exit(1);
        }
    }
}

/* Checks if hashmap is empty by looking up previously removed items */
void check_empty(int *items) {
    for (int i = 0; i < NUM_ITEMS; ++i) {
        int num = items[i];
        if (lookup_item(hm, num) == 0) {
            fprintf(stderr, "Hashmap does still contain item %d after removing it\n", num);
            free_hashmap(hm);
            free(items);
            exit(1);
        }
    }
}

void single_thread_cleanup_test() {
    int *items;

    hm = alloc_hashmap(N_BUCKETS);
    if (!hm) {
        fprintf(stderr, "Hashmap could not be allocated\n");
        exit(1);
    }

    validate();

    items = calloc(NUM_ITEMS, sizeof(int));

    fill(items);
    clear(items);
    check_empty(items);

    free_hashmap(hm);

    free(items);
}

void *thread_remove_items(void *data) {
    size_t n_items = NUM_ITEMS / N_THREADS;
    int *items = data;

    while (!go) { }

    for (size_t i = 0; i < n_items; ++i) {
        remove_item(hm, items[i]);
    }

    return NULL;
}

/* Check if multiple threads operating on the same hashmap can cleanly remove all items */
void multi_thread_cleanup_test() {
    int *items;
    pthread_t threads[N_THREADS];

    hm = alloc_hashmap(N_BUCKETS);
    if (!hm) {
        fprintf(stderr, "Hashmap could not be allocated\n");
        exit(1);
    }

    validate();

    items = calloc(NUM_ITEMS, sizeof(int));
    fill(items);

    for (size_t i = 0; i < N_THREADS; ++i) {
        pthread_create(&threads[i], NULL, thread_remove_items, items + ((NUM_ITEMS / N_THREADS) * i));
    }

    go = 1;

    for (size_t i = 0; i < N_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    check_empty(items);

    free_hashmap(hm);
    free(items);
}

int main(int argc, char **argv) {
    srand((int)time(NULL));

    single_thread_cleanup_test();
    
    multi_thread_cleanup_test();
    
    return 0;
}

