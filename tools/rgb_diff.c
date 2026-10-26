/* Compare two raw RGB24 240x160 frame streams frame by frame.
 *
 *   rgb_diff <ours.rgb> <ref.rgb> [nframes]
 *
 * Prints, per frame, the number of differing pixels, and searches a frame
 * offset in [-30,30] so a constant phase lag between the two captures shows up
 * as a sharp minimum at a non-zero offset.  A frame that matches at offset 0 is
 * a genuine rendering difference; a run that only matches at offset k means we
 * are k frames ahead of the reference.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 240
#define H 160
#define FRAME (W * H * 3)

static unsigned char *read_all(const char *p, long *nf) {
    FILE *f = fopen(p, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "short read %s\n", p); exit(1); }
    fclose(f);
    *nf = sz / FRAME;
    return b;
}

static long count_diff(const unsigned char *a, const unsigned char *b) {
    long d = 0;
    for (long i = 0; i < FRAME; i += 3)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) d++;
    return d;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <ours.rgb> <ref.rgb> [nframes] [scan] [all]\n", argv[0]); return 2; }
    long na, nb;
    unsigned char *A = read_all(argv[1], &na);
    unsigned char *B = read_all(argv[2], &nb);
    long n = na < nb ? na : nb;
    if (argc > 3) { long r = atol(argv[3]); if (r < n) n = r; }
    int scan = argc > 4 ? atoi(argv[4]) : 30;
    int all = argc > 5 && !strcmp(argv[5], "all");
    printf("frames: ours=%ld ref=%ld comparing=%ld\n", na, nb, n);

    if (all) {
        printf("frame diff_px pct\n");
        for (long f = 0; f < n; f++) {
            long d = count_diff(A + f * FRAME, B + f * FRAME);
            printf("%ld %ld %.3f\n", f, d, 100.0 * d / (W * H));
        }
        return 0;
    }

    if (!strcmp(argv[5] ? argv[5] : "", "lag")) {
        /* For windows spread over the capture, find the frame offset that best
           aligns ours with the reference.  A growing offset is a clock-rate
           difference (phase drift), not a rendering error. */
        const int win = 100, step = 8, maxoff = 250;
        printf("center_frame best_offset mean_diff_px_at_best\n");
        for (long c = win; c + win < n; c += 250) {
            long best = 0; double bestv = 1e18;
            for (int off = -maxoff; off <= maxoff; off++) {
                double sum = 0; long cnt = 0;
                for (long f = c; f < c + win; f++) {
                    long g = f + off;
                    if (g < 0 || g >= n) continue;
                    const unsigned char *a = A + f * FRAME, *b = B + g * FRAME;
                    long d = 0;
                    for (long i = 0; i < FRAME; i += 3 * step)
                        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) d++;
                    sum += (double)d; cnt++;
                }
                if (!cnt) continue;
                double avg = sum / cnt;
                if (avg < bestv) { bestv = avg; best = off; }
            }
            printf("%ld %+ld %.1f\n", c, best, bestv * step);
        }
        return 0;
    }

    /* phase scan over a window of the middle of the capture */
    long probe_start = n / 4, probe_len = n / 8 + 1;
    if (probe_start + probe_len > n) { probe_start = 0; probe_len = n < 120 ? n : 120; }
    long best_off = 0; double best_avg = 1e18;
    printf("phase scan (offset : mean diff px over %ld probe frames)\n", probe_len);
    for (int off = -scan; off <= scan; off++) {
        double sum = 0; long cnt = 0;
        for (long f = probe_start; f < probe_start + probe_len; f++) {
            long g = f + off;
            if (g < 0 || g >= n) continue;
            sum += (double)count_diff(A + f * FRAME, B + g * FRAME);
            cnt++;
        }
        if (!cnt) continue;
        double avg = sum / cnt;
        if (avg < best_avg) { best_avg = avg; best_off = off; }
        if (off % 5 == 0 || avg < 50) printf("  %+3d : %8.1f px (%.2f%%)\n", off, avg, 100.0 * avg / (W * H));
    }
    printf("best offset = %+ld (mean %.2f px, %.3f%%)\n", best_off, best_avg, 100.0 * best_avg / (W * H));

    /* per-frame detail at the best offset */
    printf("per-frame diff at offset %+ld (first 40, then every 60th):\n", best_off);
    long worst_f = -1; long worst_d = -1; double total = 0; long nf = 0;
    long over1 = 0, over5 = 0, exact = 0;
    for (long f = 0; f < n; f++) {
        long g = f + best_off;
        if (g < 0 || g >= n) continue;
        long d = count_diff(A + f * FRAME, B + g * FRAME);
        total += (double)d; nf++;
        if (d == 0) exact++;
        if (d > 240) over1++;
        if (d > 1200) over5++;
        if (d > worst_d) { worst_d = d; worst_f = f; }
        if (f < 40 || f % 60 == 0)
            printf("  f%-4ld %6ld px (%.2f%%)\n", f, d, 100.0 * d / (W * H));
    }
    printf("summary: frames=%ld exact=%ld >240px(1%%)=%ld >1200px(5%%)=%ld mean=%.2f px (%.3f%%) worst=f%ld %ld px (%.2f%%)\n",
           nf, exact, over1, over5, nf ? total / nf : 0, nf ? 100.0 * total / nf / (W * H) : 0,
           worst_f, worst_d, 100.0 * worst_d / (W * H));
    return 0;
}
