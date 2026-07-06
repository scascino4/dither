#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third-party/stb_image_write.h"

#include "table.h"
#include "dither.h"
#include "palette.h"

#include "table.c"

#define BAYER_N 8
#define MIX_N (BAYER_N * BAYER_N)

/* Ordered-dither threshold pattern. 8x8 gives 65 mix levels. */
static const unsigned char bayer[BAYER_N][BAYER_N] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};

static struct mix mixes[PAL_N * PAL_N * (MIX_N + 1)];
static int nmix;
static double lin[256];
static double pal_lin[PAL_N][3];

static double srgb_to_linear(unsigned char v)
{
    double c;

    c = (double)v / 255.0;
    if (c <= 0.04045)
        return c / 12.92;
    return pow((c + 0.055) / 1.055, 2.4);
}

static void oklab(double r, double g, double b,
                  double *out_l, double *out_a, double *out_b)
{
    double lms_l, lms_m, lms_s;
    double l, m, s;

    lms_l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    lms_m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    lms_s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    l = cbrt(lms_l);
    m = cbrt(lms_m);
    s = cbrt(lms_s);

    *out_l = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    *out_a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    *out_b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

static void init_color_tables(void)
{
    int c0, c1, i, j, m;
    double f, r, g, b;
    struct mix *x;

    for (i = 0; i < 256; i++)
        lin[i] = srgb_to_linear((unsigned char)i);

    for (i = 0; i < PAL_N; i++) {
        for (j = 0; j < 3; j++)
            pal_lin[i][j] = lin[pal[i][j]];
    }

    /* Precompute unordered palette-pair mixtures in linear light, then store OKLab. */
    nmix = 0;
    for (c0 = 0; c0 < PAL_N; c0++) {
        for (c1 = c0; c1 < PAL_N; c1++) {
            for (m = 0; m <= MIX_N; m++) {
                f = (double)m / (double)MIX_N;
                r = pal_lin[c0][0] * f + pal_lin[c1][0] * (1.0 - f);
                g = pal_lin[c0][1] * f + pal_lin[c1][1] * (1.0 - f);
                b = pal_lin[c0][2] * f + pal_lin[c1][2] * (1.0 - f);

                x = mixes + nmix++;
                x->c0 = (unsigned char)c0;
                x->c1 = (unsigned char)c1;
                x->m = (unsigned char)m;
                oklab(r, g, b, &x->l, &x->a, &x->b);
            }
        }
    }
}

/*
 * Match colors in a more perceptual space than byte sRGB.  sRGB values are
 * gamma-encoded, so arithmetic on them does not correspond to physical light;
 * first convert to linear RGB before averaging palette colors.  Then compare
 * input colors and palette mixtures in OKLab, where Euclidean distance better
 * tracks perceived color difference than raw RGB distance.
 */
static void best(unsigned char r, unsigned char g, unsigned char b,
                 unsigned char *cc0, unsigned char *cc1, unsigned char *mm)
{
    double tl, ta, tb, dl, da, db, bd, d;
    int i;
    struct mix *x, *bx;

    oklab(lin[r], lin[g], lin[b], &tl, &ta, &tb);

    bd = 1.0e30;
    bx = mixes;

    /* Find the closest precomputed mixture by squared OKLab distance. */
    for (i = 0; i < nmix; i++) {
        x = mixes + i;
        dl = tl - x->l;
        da = ta - x->a;
        db = tb - x->b;
        d = dl * dl + da * da + db * db;
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

    /* Allocate output pixels and a lookup table. */
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

    init_color_tables();

    /* Pick each pixel's best mix, then choose c0/c1 with the Bayer cell. */
    i = 0;
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
