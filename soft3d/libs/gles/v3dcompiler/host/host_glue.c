/* WS6 host-oracle glue.
 *
 * The device build resolves util_get_process_name() and util_tls_qsort_r() from
 * v3dcompiler_ewokos.c, which the host build omits (macOS supplies mkstemps,
 * pthread_getcpuclockid, open_memstream and the array new/delete operators).
 * These two, however, are mesa-internal helpers with no macOS counterpart, so
 * provide portable host versions here:
 *
 *   util_get_process_name - util/u_process.c is excluded (needs
 *     program_invocation_name / /proc); a fixed label is enough for log.c.
 *   util_tls_qsort_r - util/u_qsort.cpp is excluded; stash the re-entrant
 *     comparator + arg in TLS and call plain qsort() through a 2-arg trampoline
 *     (identical strategy to the device glue, portable across libc flavours).
 *
 * Host-only; never built for the device.
 */
#include <stddef.h>
#include <stdlib.h>

const char *
util_get_process_name(void)
{
   return "v3dcompiler-host";
}

static __thread int (*tls_qsort_compar)(const void *, const void *, void *);
static __thread void *tls_qsort_arg;

static int
tls_qsort_trampoline(const void *a, const void *b)
{
   return tls_qsort_compar(a, b, tls_qsort_arg);
}

void
util_tls_qsort_r(void *base, size_t nmemb, size_t size,
                 int (*compar)(const void *, const void *, void *),
                 void *arg)
{
   tls_qsort_compar = compar;
   tls_qsort_arg = arg;
   qsort(base, nmemb, size, tls_qsort_trampoline);
}
