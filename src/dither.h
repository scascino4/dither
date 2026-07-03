#ifndef DITHER_H
#define DITHER_H

struct mix {
    unsigned char c0, c1, m;
    double l, a, b;
};

struct result {
    unsigned char c0, c1, m;
};

#endif
