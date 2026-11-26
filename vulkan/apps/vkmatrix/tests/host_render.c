/*
 * host_render.c - host-side render/benchmark harness for the matrix effect.
 * Builds with the host cc (no EwokOS deps):
 *   cc -O2 -I.. host_render.c ../matrix.c -lm -o host_render
 * Usage: host_render <w> <h> <time> <out.rgba> [iterations] [mousex mousey] [mt [workers]]
 * Writes raw top-down RGBA8 and prints the render time.  With "mt" the frame
 * is also rendered as pthread row bands and compared byte-for-byte against
 * the single-threaded result (band independence proof) and timed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "matrix.h"

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct {
    const matrix_frame *f;
    uint32_t *buf;
    int y0, y1, worker;
} band_arg;

static void *band_proc(void *p)
{
    band_arg *a = (band_arg *)p;
    matrix_render_band(a->f, a->buf, a->y0, a->y1, a->worker);
    return NULL;
}

int main(int argc, char **argv)
{
    matrix_frame f;
    uint32_t *buf;
    double t0, t1;
    FILE *fp;
    if (argc < 5) {
        fprintf(stderr, "usage: %s w h time out.rgba [iterations] [mx my]\n", argv[0]);
        return 1;
    }
    f.width = atoi(argv[1]);
    f.height = atoi(argv[2]);
    f.time = (float)atof(argv[3]);
    f.iterations = argc > 5 ? atoi(argv[5]) : 40;
    f.mouse[0] = argc > 7 ? (float)atof(argv[6]) : 0.0f;
    f.mouse[1] = argc > 7 ? (float)atof(argv[7]) : 0.0f;
    buf = (uint32_t *)malloc((size_t)f.width * f.height * 4);
    if (!buf) return 1;
    t0 = now_sec();
    matrix_render(&f, buf);
    t1 = now_sec();
    fp = fopen(argv[4], "wb");
    if (!fp) { free(buf); return 1; }
    fwrite(buf, 4, (size_t)f.width * f.height, fp);
    fclose(fp);
    printf("%dx%d iter=%d time=%.3f mouse=(%.0f,%.0f): %.1f ms (%.2f fps)\n",
           f.width, f.height, f.iterations, f.time, f.mouse[0], f.mouse[1],
           (t1 - t0) * 1000.0, 1.0 / (t1 - t0));
    if (argc > 8 && strcmp(argv[8], "mt") == 0) {
        int nw = argc > 9 ? atoi(argv[9]) : 4;
        uint32_t *buf2 = (uint32_t *)malloc((size_t)f.width * f.height * 4);
        pthread_t tid[MATRIX_MAX_WORKERS];
        band_arg arg[MATRIX_MAX_WORKERS];
        int i;
        if (nw > MATRIX_MAX_WORKERS) nw = MATRIX_MAX_WORKERS;
        if (!buf2) { free(buf); return 1; }
        t0 = now_sec();
        for (i = 0; i < nw; i++) {
            arg[i].f = &f;
            arg[i].buf = buf2;
            arg[i].worker = i;
            arg[i].y0 = f.height * i / nw;
            arg[i].y1 = f.height * (i + 1) / nw;
            if (i > 0) pthread_create(&tid[i - 1], NULL, band_proc, &arg[i]);
        }
        band_proc(&arg[0]);
        for (i = 1; i < nw; i++) pthread_join(tid[i - 1], NULL);
        t1 = now_sec();
        printf("mt%d: %.1f ms (%.2f fps), identical=%s\n", nw,
               (t1 - t0) * 1000.0, 1.0 / (t1 - t0),
               memcmp(buf, buf2, (size_t)f.width * f.height * 4) == 0 ? "yes" : "NO");
        free(buf2);
    }
    free(buf);
    return 0;
}