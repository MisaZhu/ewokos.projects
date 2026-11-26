/*
 * host_render.c - host-side render harness for vkolddisp.
 * Builds with the host cc against the repo SPIR-V front-end (no EwokOS deps):
 *   cc -O2 -I<libs>/src/spirv host_render.c ../olddisp.c \
 *      <libs>/src/spirv/spirv_parse.c <libs>/src/spirv/spirv_interp.c \
 *      -lm -o host_render
 * Usage: host_render <w> <h> <time> <out.rgba> [year month0 day daysecs]
 * Runs the native font engine (olddisp.c) into the coverage texture, then
 * the embedded SPIR-V (verbatim Buffer B + Image) per pixel.  Writes raw
 * top-down RGBA8 (the cpu_raster.c convention: frag_coord is (px+0.5, py+0.5)
 * top-left origin) and prints render time + decline count.
 * verify_reference.py replays the two live-Shadertoy captures in reference/
 * through this probe and demands a pixel-exact match.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "spirv.h"
#include "../olddisp.h"
#include "../vkolddisp_spirv.h"

/* std140 Params UBO, mirrors shaders/vkolddisp.frag (vec4 uDate @32) */
typedef struct {
    float   resolution[2];  /* 0  */
    float   time;           /* 8  */
    int32_t frame;          /* 12 */
    float   time_delta;     /* 16 */
    float   pad[3];         /* 20 */
    float   date[4];        /* 32 */
} olddisp_ubo_t;

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint8_t clamp8(float v)
{
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

int main(int argc, char **argv)
{
    char err[256];
    spirv_module *m;
    const spirv_entry *e;
    olddisp_ubo_t ubo;
    olddisp_frame frame;
    spirv_resources res;
    uint8_t *buf, *cov, *tex;
    int w, h, x, y, declines = 0;
    double t0, t1, time;
    FILE *fp;

    if (argc < 5) {
        fprintf(stderr, "usage: %s w h time out.rgba [year month0 day daysecs]\n", argv[0]);
        return 1;
    }
    w = atoi(argv[1]); h = atoi(argv[2]); time = atof(argv[3]);

    memset(&ubo, 0, sizeof(ubo));
    ubo.resolution[0] = (float)w;
    ubo.resolution[1] = (float)h;
    ubo.time = (float)time;
    ubo.frame = 0;
    ubo.time_delta = 1.0f / 60.0f;
    if (argc >= 9) {
        ubo.date[0] = (float)atof(argv[5]);
        ubo.date[1] = (float)atof(argv[6]);
        ubo.date[2] = (float)atof(argv[7]);
        ubo.date[3] = (float)atof(argv[8]);
    } else {
        ubo.date[0] = 2024.0f; ubo.date[1] = 10.0f;
        ubo.date[2] = 27.0f;   ubo.date[3] = 45296.0f;
    }

    m = spirv_parse(kOlddispFs, sizeof(kOlddispFs) / 4, err, sizeof(err));
    if (!m) { fprintf(stderr, "parse failed: %s\n", err); return 1; }
    e = spirv_find_entry(m, "main", SPIRV_EXEC_FRAGMENT);
    if (!e) { fprintf(stderr, "no fragment entry\n"); return 1; }

    buf = (uint8_t *)malloc((size_t)w * h * 4);
    cov = (uint8_t *)malloc((size_t)olddisp_font_w(w) * olddisp_font_h(h));
    tex = (uint8_t *)malloc((size_t)w * h * 4);
    if (!buf || !cov || !tex) return 1;

    /* native font engine -> lossless coverage texture (bottom-up rows) */
    frame.width = w; frame.height = h;
    frame.itime = (float)time;
    memcpy(frame.date, ubo.date, sizeof(frame.date));
    olddisp_render(&frame, cov);
    olddisp_encode(&frame, cov, tex);

    t0 = now_sec();
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t *px = buf + ((size_t)y * w + x) * 4;
            memset(&res, 0, sizeof(res));
            res.bindings[0].set = 0;
            res.bindings[0].binding = 0;
            res.bindings[0].kind = SPIRV_RES_UNIFORM_BUFFER;
            res.bindings[0].data = &ubo;
            res.bindings[0].size = sizeof(ubo);
            res.bindings[1].set = 0;
            res.bindings[1].binding = 1;
            res.bindings[1].kind = SPIRV_RES_COMBINED_IMAGE_SAMPLER;
            res.bindings[1].data = tex;
            res.bindings[1].width = (uint32_t)w;
            res.bindings[1].height = (uint32_t)h;
            res.bindings[1].bpp = 4;
            res.num_bindings = 2;
            res.frag_coord[0] = (float)x + 0.5f;
            res.frag_coord[1] = (float)y + 0.5f;
            res.frag_coord[2] = 0.0f;
            res.frag_coord[3] = 1.0f;
            res.front_facing = 1;
            if (spirv_run(m, e, &res) != 0) {
                declines++;
                px[0] = px[1] = px[2] = 0; px[3] = 255;
                continue;
            }
            px[0] = clamp8(res.out_loc[0][0]);
            px[1] = clamp8(res.out_loc[0][1]);
            px[2] = clamp8(res.out_loc[0][2]);
            px[3] = 255;
        }
    }
    t1 = now_sec();

    fp = fopen(argv[4], "wb");
    if (!fp) { free(buf); free(cov); free(tex); return 1; }
    fwrite(buf, 4, (size_t)w * h, fp);
    fclose(fp);
    printf("%dx%d time=%.3f: %.1f ms (%.2f fps), declines=%d\n",
           w, h, time, (t1 - t0) * 1000.0, 1.0 / (t1 - t0), declines);
    free(buf);
    free(cov);
    free(tex);
    spirv_free(m);
    return declines ? 2 : 0;
}