/* Regression test for fb_text(): the 8x8 font only has the first 128 code
 * points, so a byte >= 0x80 in a ROM name (UTF-8) or in the ROM header title
 * (Shift-JIS) used to index past the table and draw random glyphs.
 *
 *   gcc -std=c99 -Wall -Wextra -Isrc tools/fb_text_test.c src/fb.c -lm \
 *       -o /tmp/fb_text_test && /tmp/fb_text_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/fb.h"

static void render(const char *s, unsigned char *out) {
    FB fb; fb_init(&fb, 200, 32); fb_fill(&fb, 0);
    fb_text(&fb, 0, 0, s, 0xFFFFFFFF);
    memcpy(out, fb.px, 200u * 32u * 4u);
    free(fb.px);
}
static int same(const char *a, const char *b) {
    static unsigned char x[200*32*4], y[200*32*4];
    render(a, x); render(b, y);
    return memcmp(x, y, sizeof x) == 0;
}
int main(void) {
    int fails = 0;
    struct { const char *a, *b, *what; } cases[] = {
        { "Pok\xC3\xA9mon Esmeralda", "Pokemon Esmeralda", "utf8 e-acute -> e" },
        { "Pok\xE9mon",               "Pokemon",            "latin1 e-acute -> e" },
        { "a\xC3\xB1o",               "ano",                "utf8 n-tilde -> n" },
        { "\x82\xA0\x82\xA2",         "????",               "shift-jis -> ?" },
        { "\xC3",                     "A",                  "truncated utf8 -> A" },
        { "\xFF\xFE",                 "y?",                 "latin1 y-diaeresis/thorn" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int ok = same(cases[i].a, cases[i].b);
        printf("%-24s %s\n", cases[i].what, ok ? "ok" : "FAIL");
        fails += !ok;
    }
    printf("%d fail(s)\n", fails);
    return fails != 0;
}
