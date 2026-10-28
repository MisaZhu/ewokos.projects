/* Host validation against the real GLES interpreter, not a replacement renderer. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../../../libs/gles/src/glsl/glsl.h"
#include "../gen/tuner_glsl.h"

static int failures;
static glsl_program *link_source(const char *source)
{
    char error[512] = {0};
    glsl_stage *vs = glsl_compile_stage(tuner_vs_glsl, 1, error, sizeof(error));
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

static void black_texture(int unit, int dim, const float *coord,
                          const glsl_tex_derivatives *deriv, float rgba[4], void *user)
{
    (void)unit; (void)dim; (void)coord; (void)deriv; (void)user;
    memset(rgba, 0, 4 * sizeof(float));
}

static void pixel(glsl_program *p, glsl_cell *uniforms, float x, float y, float out[4])
{
    float coord[4] = {x, y, 0, 1}, point[2] = {0, 0}, depth;
    int discard = 0, wrote = 0;
    glsl_run_fragment(p, NULL, NULL, NULL, uniforms, coord, point, NULL, NULL, 1, black_texture, NULL,
                      out, &discard, &depth, &wrote);
}

static void regression(const char *name, const char *src, const float want[4])
{
    glsl_program *p = link_source(src);
    int ok = p != NULL;
    if (p) {
        glsl_cell uniforms[4] = {{0}};
        float out[4];
        pixel(p, uniforms, 0.5f, 0.5f, out);
        for (int i = 0; i < 4; i++)
            if (!isfinite(out[i]) || fabsf(out[i] - want[i]) > 1e-6f) ok = 0;
        if (!ok) fprintf(stderr, "got %.9g %.9g %.9g %.9g\n", out[0],out[1],out[2],out[3]);
        glsl_free_program(p);
    }
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    failures += !ok;
}

int main(int argc, char **argv)
{
    const float yes[4] = {1,1,1,1};
    regression("unsigned PCG arithmetic", "void main(){ uvec3 s=uvec3(0xffffffffu,2147483648u,1u);"
        "s=s*1145141919u+1919810u; s^=s>>16;"
        "gl_FragColor=vec4(s.x==3151719288u,s.y==2149436255u,s.z==1147044287u,1);}", yes);
    regression("integer constructors preserve low bits", "void main(){"
        "uvec2 a=uvec2(0xffffffffu,16777217u); ivec2 b=ivec2(16777217,2147483647);"
        "gl_FragColor=vec4(a.x==0xffffffffu,a.y==16777217u,b.x==16777217,b.y==2147483647);}", yes);
    regression("bitcasts and round", "void main(){"
        "uvec3 s=floatBitsToUint(vec3(-50.,.5,1.));"
        "gl_FragColor=vec4(s.x==0xc2480000u,s.y==0x3f000000u,s.z==0x3f800000u,round(1.2));}", yes);
    regression("nested struct layout and swizzle-like field names",
        "struct G{vec3 s; vec3 c; vec3 h; int i; float d;};"
        "struct M{vec4 isect; vec3 rp; float rl; G grid;};"
        "M f(G g){M m;m.grid=g;m.rp=vec3(7,8,9);m.rl=10.;m.isect=vec4(1,2,3,4);return m;}"
        "void main(){G g;g.s=vec3(2,3,4);g.c=vec3(5);g.h=vec3(6);g.i=2;g.d=11.;"
        "M m=f(g);gl_FragColor=vec4(m.grid.s.z==4.,m.grid.i==2,m.rp.y==8.,m.grid.d==11.);}", yes);
    regression("unsigned division, remainder and comparisons", "void main(){"
        "uint a=0xffffffffu;gl_FragColor=vec4(a/3u==1431655765u,a%7u==3u,"
        "a>2147483648u,2147483648u>=2147483648u);}", yes);
    regression("unsigned indexing, swizzles and out parameters",
        "void swap(inout uvec2 a){a=a.yx;} void main(){uvec2 a=uvec2(1u,0xffffffffu);"
        "swap(a);a[1]+=1u;a.x>>=31;gl_FragColor=vec4(a.x==1u,a.y==2u,~0u==0xffffffffu,-1u==0xffffffffu);}", yes);
    regression("unsigned globals and arrays",
        "const uint base=2147483648u; uint f(uint a){return a>>31;}"
        "void main(){uint a[2]=uint[2](base,16777217u);"
        "gl_FragColor=vec4(f(a[0])==1u,a[1]==16777217u,float(base)==2147483648.,uint(42.)==42u);}", yes);
    regression("float vectors and matrices retain constructor behavior", "void main(){"
        "vec3 a=vec3(ivec2(2,3),4);mat3 m=mat3(2);vec3 b=m*a;"
        "gl_FragColor=vec4(b.x==4.,b.y==6.,b.z==8.,round(-1.2)==-1.);}", yes);
    if (argc > 1 && !strcmp(argv[1], "--regressions")) return failures ? 1 : 0;

    int full = 0, compare = 0;
    const char *reference_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--full")) full = 1;
        else if (!strcmp(argv[i], "--compare")) compare = 1;
        else if (!reference_dir) reference_dir = argv[i];
        else { fprintf(stderr, "Usage: shader-test [reference-dir] [--full] [--compare]\n"); return 1; }
    }
    glsl_program *p = link_source(tuner_fs_glsl);
    if (!p) return 1;
    printf("tuner: functions=%d globals=%d\n", p->fs->nfuncs, p->fs->nglobals);
    glsl_cell *u = calloc(p->uniform_block_floats, sizeof(*u));
    int ti = glsl_uniform_index(p, "iTime"), ri = glsl_uniform_index(p, "iResolution");
    if (!u || ti < 0 || ri < 0) return 1;
    glsl_program *original = compare ? link_source(tuner_reference_fs_glsl) : NULL;
    glsl_cell *ou = original ? calloc(original->uniform_block_floats, sizeof(*ou)) : NULL;
    if (compare && (!original || !ou)) return 1;
    int width = 320, height = 180;
    u[p->uniforms[ri].offset].f = width;
    u[p->uniforms[ri].offset+1].f = height;
    u[p->uniforms[ri].offset+2].f = 1;
    const int times[] = {0,1,4,8,16,32};
    for (int t = 0; t < (full ? 1 : 6); t++) {
        FILE *ref = NULL;
        if (reference_dir) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/ref-%d.rgba", reference_dir, times[t]);
            ref = fopen(path, "rb");
            if (!ref) { perror(path); return 1; }
        }
        u[p->uniforms[ti].offset].f = times[t];
        double error = 0; int count = 0, nonzero = 0, mismatches = 0;
        if (original) {
            int oti = glsl_uniform_index(original, "iTime"), ori = glsl_uniform_index(original, "iResolution");
            ou[original->uniforms[oti].offset].f = times[t];
            ou[original->uniforms[ori].offset].f = width;
            ou[original->uniforms[ori].offset+1].f = height;
            ou[original->uniforms[ori].offset+2].f = 1;
        }
        for (int y = full ? 0 : 5; y < height; y += full ? 1 : 17)
        for (int x = full ? 0 : 7; x < width; x += full ? 1 : 19) {
            float out[4]; unsigned char rgb[4] = {0};
            pixel(p, u, x+.5f, y+.5f, out);
            if (original) {
                float expected[4];
                pixel(original, ou, x+.5f, y+.5f, expected);
                if (memcmp(out, expected, sizeof(out))) ++mismatches;
            }
            for (int c = 0; c < 4; c++) if (!isfinite(out[c])) failures++;
            if (out[3] != 1) failures++;
            nonzero += out[0]+out[1]+out[2] > .01f;
            if (ref) {
                fseek(ref, (y * width + x) * 4, SEEK_SET);
                if (fread(rgb, 1, 4, ref) != 4) return 1;
                for (int c = 0; c < 3; c++) error += fabs(fmin(1,fmax(0,out[c]))*255-rgb[c]);
            }
            count++;
        }
        if (ref) fclose(ref);
        printf("t=%d samples=%d nonblack=%d RGB MAE=%.6f/255\n", times[t], count, nonzero, error/(count*3));
        if (original) {
            printf("original/optimized exact float mismatches: %d/%d\n", mismatches, count);
            failures += mismatches != 0;
        }
        if ((!full && nonzero != count) || nonzero == 0 ||
            (ref && error/(count*3) > 3.0)) failures++;
    }
    free(ou); glsl_free_program(original);
    free(u); glsl_free_program(p);
    return failures ? 1 : 0;
}
