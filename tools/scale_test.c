/*
 * scale_test.c - unit test for fb_fit_scale().
 *
 * The picture always follows the client area (resize, maximize, fullscreen)
 * keeping the 3:2 aspect. snap!=0 rounds down to a whole multiple from 1x up
 * ("crisp"); snap==0 fills the area exactly ("Stretch Fit To Screen").
 *
 *   gcc -Isrc tools/scale_test.c src/fb.c -o scale_test && ./scale_test
 */
#include "fb.h"
#include <math.h>
#include <stdio.h>

static int fails;

static void expect(int aw, int ah, int snap, double want) {
    double got = fb_fit_scale(aw, ah, snap);
    int ok = fabs(got - want) < 1e-6;
    printf("%s fb_fit_scale(%4d,%4d,snap=%d) = %.4f  want %.4f\n",
           ok ? "ok  " : "FAIL", aw, ah, snap, got, want);
    if (!ok) fails++;
}

int main(void) {
    expect(720, 480, 1, 3.0);        /* default window: exact 3x */
    expect(1920, 1080, 1, 6.0);      /* maximized: 6.75 -> whole 6 */
    expect(1920, 1080, 0, 6.75);     /* stretch: fills the window */
    expect(2560, 1440, 1, 9.0);      /* 9x fits exactly */
    expect(240, 160, 1, 1.0);        /* native size */
    expect(400, 300, 1, 1.0);        /* 1.66 -> 1 */
    expect(400, 300, 0, 400.0 / 240.0);
    expect(120, 80, 1, 0.5);         /* below 1x keeps the fraction */
    expect(120, 300, 0, 0.5);        /* width-limited */
    expect(0, 0, 1, 0.0);            /* degenerate area */
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
