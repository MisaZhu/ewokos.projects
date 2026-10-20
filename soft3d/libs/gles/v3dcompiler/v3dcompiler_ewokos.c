/* EwokOS platform glue for the mesa GLSL->QPU compiler (libv3dcompiler.a).
 *
 * These are the handful of symbols libv3dcompiler.a needs that the EwokOS SDK
 * does not provide. They are compiled into the archive by THIS Makefile, which
 * is the EwokOS device build only (it is gated on the PGL_G3D capability that
 * just the raspi5 platform make.inc exports), so the compiler is self-contained:
 * an app links libv3dcompiler.a and resolves everything with no extra -l flags
 * and no dependency on the daemon-side libbsp.
 *
 * This glue is kept as a separate translation unit rather than folded into the
 * portable stubs (v3dcompiler_stubs.c) because it is the single EwokOS-specific
 * object in the archive: a glibc/macOS host supplies every symbol here, so the
 * stubs stay portable while this file fills the EwokOS-SDK libc/util gaps.
 *
 * The vendored mesa code itself stays platform-independent, and nothing here is
 * board-specific either - these are EwokOS-SDK libc/util gaps, identical on any
 * EwokOS machine that had a V3D compiler. Upstream mesa pin
 * f45825a0c724d06f231a1210484fe9af71a3ffab. See VENDORED.txt.
 */
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* util/log.c labels its log file and syslog ident with util_get_process_name(),
 * whose real home util/u_process.c is excluded (needs program_invocation_name /
 * /proc). A fixed label suffices - it only names the on-device shader log. */
const char *
util_get_process_name(void)
{
   return "v3dcompiler";
}

/* util/u_qsort.cpp is excluded: it needs C++ <thread>, which EWOK_STL lacks.
 * u_qsort.h's inline util_qsort_r() therefore takes its final fallback branch
 * (none of HAVE_GNU_QSORT_R / HAVE_BSD_QSORT_R / HAVE_QSORT_S is defined, and the
 * EwokOS SDK <stdlib.h> exposes only qsort(), not qsort_r()). Provide that
 * fallback: stash the re-entrant comparator and its argument in thread-local
 * storage, then call plain qsort() through a two-argument trampoline. This is
 * u_qsort.cpp's own strategy minus the std::thread parallelism, which the
 * single-threaded on-device compile does not need. EwokOS supports __thread
 * (libewoksys emutls). */
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

/* util/log.c calls mkstemps() only under the MESA_LOG_FILE_AUTO env var, which
 * is never set for an on-device app compile; the EwokOS SDK provides mkstemp()
 * but not the suffix-aware mkstemps(). Fail cleanly (fd < 0): log.c then leaves
 * mesa_log_file on stderr, which is the desired on-device behaviour anyway. */
int
mkstemps(char *tmpl, int suffixlen)
{
   (void)tmpl;
   (void)suffixlen;
   errno = ENOSYS;
   return -1;
}

/* util/u_thread.c's util_thread_get_time_nano() (a perf-timing helper reached
 * through u_queue) calls pthread_getcpuclockid() under HAVE_PTHREAD. The EwokOS
 * SDK has pthreads and clock_gettime() but not the per-thread CPU clock. Hand
 * back CLOCK_MONOTONIC so the caller's subsequent clock_gettime() yields elapsed
 * wall time - fine for a timing delta - and the symbol stays resolved. */
int
pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
   (void)thread;
   if (clock_id == NULL)
      return EINVAL;
#ifdef CLOCK_MONOTONIC
   *clock_id = CLOCK_MONOTONIC;
#else
   *clock_id = CLOCK_REALTIME;
#endif
   return 0;
}

/* util/memstream.c's u_memstream_open() calls open_memstream() on the POSIX
 * path; the EwokOS SDK provides none of open_memstream / fmemopen /
 * fopencookie, so there is no portable way to back a growable in-memory FILE*.
 * Its only callers are nir_print.c's nir_shader_as_str / nir_instr_as_str debug
 * helpers, which guard on the bool return and simply yield an empty string when
 * the stream could not be opened. Returning NULL is therefore a clean, safe
 * "unsupported": compilation is unaffected, only a debug NIR dump comes back
 * empty. */
FILE *
open_memstream(char **bufp, size_t *sizep)
{
   (void)bufp;
   (void)sizep;
   return NULL;
}

/* EwokOS libcxx supplies the SCALAR C++ allocation operators (_Znwm = operator
 * new, _ZdlPv = operator delete) as weak symbols but omits the ARRAY forms,
 * which libv3dcompiler.a's C++ mesa code references (e.g. opt_function_inlining's
 * ir_call::generate_inline does `new ir_instruction[n]` / `delete[]`). Provide
 * them here, defined with their Itanium-ABI mangled names (a C identifier is
 * emitted verbatim, so it matches the C++ reference) and implemented by
 * delegating to libcxx's scalar operators - identical allocation semantics, no
 * array cookie needed because every mesa use pairs new[] with a matching
 * delete[] of a trivially-destructible or explicitly-destructed type.
 *
 * They are WEAK: libewokstl.a (which every EwokOS C++ app links) also defines
 * the array operators strongly, and now that libv3dcompiler.a is merged into
 * libGLESv2.a both land in a single link. Weak lets libewokstl's strong
 * definition win, while still leaving a self-contained fallback for a link with
 * no C++ runtime library. */
extern void *_Znwm(unsigned long);   /* operator new(unsigned long)  - libcxx */
extern void  _ZdlPv(void *);         /* operator delete(void*)       - libcxx */

__attribute__((weak)) void *
_Znam(unsigned long n)               /* operator new[](unsigned long) */
{
   return _Znwm(n);
}

__attribute__((weak)) void
_ZdaPv(void *p)                      /* operator delete[](void*) */
{
   _ZdlPv(p);
}
