#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third-party/stb_image_write.h"

#include "cache.h"
#include "dither.h"
#include "palette.h"

#include "cache.c"

#define BAYER_N 4
#define MIX_N (BAYER_N * BAYER_N)

/* Ordered-dither threshold pattern. */
static const unsigned char bayer[BAYER_N][BAYER_N] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
};

static struct mix mixes[PAL_N * PAL_N * (MIX_N + 1)];
static int nmix;

static void init_mixes(void)
{
    int c0, c1, m;
    struct mix *x;

    /* Precompute every palette-pair mixture in MIX_N-scaled RGB. */
    nmix = 0;
    for (c0 = 0; c0 < PAL_N; c0++) {
        for (c1 = 0; c1 < PAL_N; c1++) {
            for (m = 0; m <= MIX_N; m++) {
                x = mixes + nmix++;
                x->c0 = (unsigned char)c0;
                x->c1 = (unsigned char)c1;
                x->m = (unsigned char)m;
                x->r = pal[c0][0] * m + pal[c1][0] * (MIX_N - m);
                x->g = pal[c0][1] * m + pal[c1][1] * (MIX_N - m);
                x->b = pal[c0][2] * m + pal[c1][2] * (MIX_N - m);
            }
        }
    }
}

static void best(unsigned char r, unsigned char g, unsigned char b,
                 unsigned char *cc0, unsigned char *cc1, unsigned char *mm)
{
    long bd, d;
    int i, dr, dg, db, rr, gg, bb;
    struct mix *x, *bx;

    rr = r * MIX_N;
    gg = g * MIX_N;
    bb = b * MIX_N;

    bd = LONG_MAX;
    bx = mixes;

    /* Find the closest precomputed mixture by squared RGB distance. */
    for (i = 0; i < nmix; i++) {
        x = mixes + i;
        dr = rr - x->r;
        dg = gg - x->g;
        db = bb - x->b;
        d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < bd) {
            bd = d;
            bx = x;
        }
    }

    *cc0 = bx->c0;
    *cc1 = bx->c1;
    *mm = bx->m;
}

static int lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t slen, suffix_len;

    slen = strlen(s);
    suffix_len = strlen(suffix);
    if (suffix_len > slen)
        return 0;

    s += slen - suffix_len;
    while (*suffix) {
        if (lower((unsigned char)*s) != lower((unsigned char)*suffix))
            return 0;
        s++;
        suffix++;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int w, h, n, x, y, rc, found;
    size_t npx, sz, i;
    unsigned char *in, *out, *p, *q;
    unsigned char c;
    unsigned long key, cap, mask;
    struct entry *tab, *e;
    struct result *vals, *v;

    rc = 1;
    in = NULL;
    out = NULL;
    tab = NULL;
    vals = NULL;

    if (argc != 3) {
        fprintf(stderr, "usage: %s image-in image-out.{jpg,jpeg,png}\n", argv[0]);
        goto done;
    }

    /* Load input as RGB, ignoring any alpha channel. */
    in = stbi_load(argv[1], &w, &h, &n, 3);
    if (!in) {
        fprintf(stderr, "%s: %s\n", argv[1], stbi_failure_reason());
        goto done;
    }

    /* Allocate output pixels and a cache. */
    npx = (size_t)w * (size_t)h;
    sz = npx * 3;
    out = (unsigned char *)malloc(sz);
    cap = 1;
    while (cap < (unsigned long)npx * 2)
        cap <<= 1;
    tab = (struct entry *)calloc((size_t)cap, sizeof(*tab));
    vals = (struct result *)malloc((size_t)cap * sizeof(*vals));
    if (!out || !tab || !vals) {
        fprintf(stderr, "out of memory\n");
        goto done;
    }
    mask = cap - 1;

    /* Pick each pixel's best mix, then choose c0/c1 with the Bayer cell. */
    i = 0;
    init_mixes();
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++, i++) {
            p = in + i * 3;
            q = out + i * 3;
            key = ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
            e = lookup(tab, mask, key, &found);
            v = vals + (e - tab);
            if (!found)
                best(p[0], p[1], p[2], &v->c0, &v->c1, &v->m);
            c = bayer[y % BAYER_N][x % BAYER_N] < v->m ? v->c0 : v->c1;
            q[0] = pal[c][0];
            q[1] = pal[c][1];
            q[2] = pal[c][2];
        }
    }

    /* Write the dithered RGB image based on the output extension. */
    if (ends_with(argv[2], ".png")) {
        if (!stbi_write_png(argv[2], w, h, 3, out, w * 3)) {
            fprintf(stderr, "%s: write failed\n", argv[2]);
            goto done;
        }
    } else if (ends_with(argv[2], ".jpg") || ends_with(argv[2], ".jpeg")) {
        if (!stbi_write_jpg(argv[2], w, h, 3, out, 95)) {
            fprintf(stderr, "%s: write failed\n", argv[2]);
            goto done;
        }
    } else {
        fprintf(stderr, "%s: unsupported output format (use .jpg, .jpeg, or .png)\n", argv[2]);
        goto done;
    }

    rc = 0;

done:
    stbi_image_free(in);
    free(out);
    free(tab);
    free(vals);
    return rc;
}
