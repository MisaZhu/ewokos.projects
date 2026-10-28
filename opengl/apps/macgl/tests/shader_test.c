/* Run the shipped shader through the actual GLES interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../../../libs/gles/src/glsl/glsl.h"
#define MACGL_TEST_REFERENCE
#include "../gen/macgl_glsl.h"

static int texture_failures;
static void data_texture(int unit, int dim, const float *uv,
                         const glsl_tex_derivatives *deriv, float out[4], void *user)
{
    (void)deriv; (void)user;
    if (unit != 0 || dim != 2 || !isfinite(uv[0]) || !isfinite(uv[1]) ||
        uv[0] < 0 || uv[0] >= 1 || uv[1] < 0 || uv[1] >= 1) {
        ++texture_failures;
        memset(out, 0, sizeof(float) * 4);
        return;
    }
    int x = (int)(uv[0] * 4), y = (int)(uv[1] * 512);
    for (int c = 0; c < 4; ++c) out[c] = macgl_data[(y*4+x)*4+c] / 255.0f;
}

static glsl_program *link_source(const char *source)
{
    char error[512] = {0};
    glsl_stage *vs = glsl_compile_stage(macgl_vs_glsl, 1, error, sizeof(error));
    glsl_stage *fs = glsl_compile_stage(source, 0, error, sizeof(error));
    if (!vs || !fs) {
        fprintf(stderr, "compile: %s\n", error);
        glsl_free_stage(vs); glsl_free_stage(fs);
        return NULL;
    }
    glsl_program *p = glsl_link(vs, fs, error, sizeof(error));
    if (!p) fprintf(stderr, "link: %s\n", error);
    return p;
}

static void pixel(glsl_program *p, glsl_cell *u, float x, float y, float out[4])
{
    float coord[4] = {x, y, 0, 1}, point[2] = {0, 0}, depth;
    int discard = 0, wrote = 0;
    glsl_run_fragment(p, NULL, NULL, NULL, u, coord, point, NULL, NULL, 1, data_texture, NULL,
                      out, &discard, &depth, &wrote);
}

int main(int argc, char **argv)
{
    const char *reference_dir = NULL;
    int full_time = -1, gpu_lookups = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strncmp(argv[i], "--full=", 7)) {
            char *end;
            long value = strtol(argv[i] + 7, &end, 10);
            if (end == argv[i] + 7 || *end || value < 0 || value > 57) return 1;
            full_time = (int)value;
        }
        else if (!strcmp(argv[i], "--gpu-lookups")) gpu_lookups = 1;
        else if (!reference_dir) reference_dir = argv[i];
        else return 1;
    }
    glsl_program *p = link_source(gpu_lookups ? macgl_fs_glsl : macgl_cpu_fs_glsl);
    glsl_program *original = link_source(macgl_reference_fs_glsl);
    if (!p || !original) return 1;
    glsl_cell *ou = calloc(original->uniform_block_floats, sizeof(*ou));
    int oti = glsl_uniform_index(original, "iTime"), ori = glsl_uniform_index(original, "iResolution");
    if (!ou || oti < 0 || ori < 0) return 1;
    printf("macgl: functions=%d globals=%d\n", p->fs->nfuncs, p->fs->nglobals);
    glsl_cell *u = calloc(p->uniform_block_floats, sizeof(*u));
    int ti = glsl_uniform_index(p, "iTime"), ri = glsl_uniform_index(p, "iResolution");
    if (!u || ti < 0 || ri < 0) return 1;
    const int width = 320, height = 180;
    u[p->uniforms[ri].offset].f = width;
    u[p->uniforms[ri].offset+1].f = height;
    u[p->uniforms[ri].offset+2].f = 1;
    int di = glsl_uniform_index(p, "macglData");
    if (gpu_lookups && di < 0) return 1;
    if (di >= 0) u[p->uniforms[di].offset].i = 0;
    ou[original->uniforms[ori].offset].f = width;
    ou[original->uniforms[ori].offset+1].f = height;
    ou[original->uniforms[ori].offset+2].f = 1;
    float warmup[4];
    pixel(p, u, .5f, .5f, warmup);
    pixel(original, ou, .5f, .5f, warmup);
    clock_t optimized_ticks = 0, original_ticks = 0;
    const int times[] = {0,1,3,7,10,16,18,20,24,26,27,30,31,32,40,49,50,54,56,57};
    FILE *samples = NULL;
    if (!reference_dir) {
        if (full_time >= 0) {
            fprintf(stderr, "Full-frame comparison requires a reference directory\n");
            return 1;
        }
        samples = fopen("tests/reference.rgb", "rb");
        if (!samples) { perror("tests/reference.rgb"); return 1; }
    }
    int failures = 0, tested = 0;
    for (unsigned t = 0; t < sizeof(times)/sizeof(times[0]); ++t) {
        if (full_time >= 0 && full_time != times[t]) continue;
        unsigned char *ref = NULL;
        if (reference_dir) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/ref-%d.rgba", reference_dir, times[t]);
            FILE *f = fopen(path, "rb");
            if (!f) { perror(path); return 1; }
            ref = malloc(width * height * 4);
            if (!ref || fread(ref, 1, width * height * 4, f) != (size_t)width*height*4 || fgetc(f) != EOF) return 1;
            fclose(f);
        } else {
            ref = calloc(width * height, 4);
            if (!ref) return 1;
            for (int y = 5; y < height; y += 17)
                for (int x = 7; x < width; x += 19)
                    if (fread(ref + (y*width+x)*4, 1, 3, samples) != 3) return 1;
        }
        ++tested;
        u[p->uniforms[ti].offset].f = times[t];
        ou[original->uniforms[oti].offset].f = times[t];
        double error_sum = 0, max_error = 0;
        int count = 0, bad = 0, mismatches = 0;
        for (int y = full_time >= 0 ? 0 : 5; y < height; y += full_time >= 0 ? 1 : 17)
        for (int x = full_time >= 0 ? 0 : 7; x < width; x += full_time >= 0 ? 1 : 19) {
            float out[4], expected[4];
            clock_t begin = clock();
            pixel(p, u, x+.5f, y+.5f, out);
            optimized_ticks += clock() - begin;
            begin = clock();
            pixel(original, ou, x+.5f, y+.5f, expected);
            original_ticks += clock() - begin;
            if (memcmp(out, expected, sizeof(out))) ++mismatches;
            for (int c = 0; c < 4; ++c)
                if (!isfinite(out[c])) ++failures;
            if (out[3] != 1) ++failures;
            if (ref) {
                for (int c = 0; c < 3; ++c) {
                    double e = fabs(fmin(1,fmax(0,out[c]))*255 - ref[(y*width+x)*4+c]);
                    error_sum += e;
                    if (e > max_error) max_error = e;
                    if (e > 2) ++bad;
                }
            }
            ++count;
        }
        printf("t=%d samples=%d RGB MAE=%.6f/255 max=%.6f channels>2=%d%s\n",
               times[t], count, error_sum/(count*3), max_error, bad,
               ref ? " (WebGL2 reference)" : " (finite/opaque only)");
        printf("original/optimized exact float mismatches: %d/%d\n", mismatches, count);
        if (mismatches || (ref && (error_sum/(count*3) > 0.6 || bad > 0))) ++failures;
        free(ref);
        fflush(stdout);
    }
    if (!tested) ++failures;
    if (samples) {
        if (fgetc(samples) != EOF) ++failures;
        fclose(samples);
    }
    /* Verify the exact loop boundary in the running interpreter. */
    float a[4], b[4];
    u[p->uniforms[ti].offset].f = 0;
    pixel(p, u, 160.5f, 90.5f, a);
    u[p->uniforms[ti].offset].f = 57;
    pixel(p, u, 160.5f, 90.5f, b);
    if (memcmp(a, b, sizeof(a))) ++failures;
    free(ou);
    glsl_free_program(original);
    free(u);
    glsl_free_program(p);
    failures += texture_failures;
    printf("CPU render time: original=%.3fms optimized=%.3fms speedup=%.2fx\n",
           original_ticks * 1000.0 / CLOCKS_PER_SEC, optimized_ticks * 1000.0 / CLOCKS_PER_SEC,
           optimized_ticks ? (double)original_ticks / optimized_ticks : 0);
    printf("macgl: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
