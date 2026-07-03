#ifndef CACHE_H
#define CACHE_H

struct entry {
    unsigned long key;
    int used;
};

struct entry *lookup(struct entry *tab, unsigned long mask,
                     unsigned long key, int *found);

#endif
