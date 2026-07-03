#include "cache.h"

/*
 * Knuth-style multiplicative hash for integer keys.
 */
static unsigned long hash(unsigned long x)
{
    x ^= x >> 11;
    x *= 2654435761UL;
    return x;
}

struct entry *lookup(struct entry *tab, unsigned long mask,
                     unsigned long key, int *found)
{
    struct entry *e;
    unsigned long i;

    i = hash(key) & mask;
    for (;;) {
        e = tab + i;
        if (!e->used) {
            e->used = 1;
            e->key = key;
            *found = 0;
            return e;
        }
        if (e->key == key) {
            *found = 1;
            return e;
        }
        i = (i + 1) & mask;
    }
}
