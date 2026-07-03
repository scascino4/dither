#ifndef PALETTE_H
#define PALETTE_H

#define PAL_N 16

static const unsigned char pal[PAL_N][3] = {
    {  0,   0,   0}, /* black */
    {  0,   0, 170}, /* blue */
    {  0, 170,   0}, /* green */
    {  0, 170, 170}, /* cyan */
    {170,   0,   0}, /* red */
    {170,   0, 170}, /* magenta */
    {170,  85,   0}, /* brown */
    {170, 170, 170}, /* light gray */
    { 85,  85,  85}, /* dark gray */
    { 85,  85, 255}, /* light blue */
    { 85, 255,  85}, /* light green */
    { 85, 255, 255}, /* light cyan */
    {255,  85,  85}, /* light red */
    {255,  85, 255}, /* light magenta */
    {255, 255,  85}, /* yellow */
    {255, 255, 255}  /* white */
};

#endif
