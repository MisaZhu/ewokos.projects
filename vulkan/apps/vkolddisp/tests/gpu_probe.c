/* gpu_probe.c - host check: does the vkolddisp VS/FS pair fit the GPU
 * transpiler subset (spirv_to_geom_kernels)?  Prints accept/reject + reason,
 * and dumps the generated raster kernel for inspection / syntax checking.
 * cc -O2 -I<libs>/src/spirv gpu_probe.c spirv_parse.c spirv_to_glsl.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spirv.h"
#include "spirv_to_glsl.h"
#include "../vkolddisp_spirv.h"

int main(int argc, char **argv) {
    char err[256] = {0};
    spirv_geom_kernels k;
    memset(&k, 0, sizeof k);
    spirv_module *vm = spirv_parse(kOlddispVs, sizeof kOlddispVs / 4, err, sizeof err);
    if (!vm) { printf("VS parse fail: %s\n", err); return 1; }
    spirv_module *fm = spirv_parse(kOlddispFs, sizeof kOlddispFs / 4, err, sizeof err);
    if (!fm) { printf("FS parse fail: %s\n", err); return 1; }
    const spirv_entry *vs = spirv_find_entry(vm, "main", SPIRV_EXEC_VERTEX);
    const spirv_entry *fs = spirv_find_entry(fm, "main", SPIRV_EXEC_FRAGMENT);
    if (!vs || !fs) { printf("entry find fail\n"); return 1; }
    int rc = spirv_to_geom_kernels(vm, vs, fm, fs, &k, err, sizeof err);
    printf("geom_kernels rc=%d err=%s\n", rc, err);
    if (rc == 0) {
        printf("layout: attrs=%d varyings=%d samplers=%d tpw=%u flat=%d\n",
               k.layout.num_attrs, k.layout.num_varyings, k.layout.num_samplers,
               k.layout.geom_tpw, k.layout.num_flat);
        for (int i = 0; i < k.layout.num_flat; i++)
            printf("  flat %s off=%u comps=%u\n", k.layout.flat[i].name,
                   k.layout.flat[i].offset, k.layout.flat[i].comps);
        if (argc > 1) {
            FILE *f = fopen(argv[1], "w");
            fprintf(f, "%s\n/* ===== raster ===== */\n%s\n", k.xform, k.raster);
            fclose(f);
            printf("dumped %s\n", argv[1]);
        }
    }
    return 0;
}