/*
 * qshim.c - minimal qcommon host shim for the standalone yquake2 gl3
 * refresher port (projects/soft3d/apps/yquake2).
 *
 * The gl3 refresher + shared files/ loaders reference a small set of qcommon
 * symbols that normally live in the engine's client/common layer, which this
 * "refresher only" port does not vendor.  Everything else gl3 needs
 * (Com_Printf, Com_Error, Com_DPrintf, R_Printf, Com_sprintf, the Vector,
 * Angle, Q_str, Little and Swap_Init helpers, GetRefAPI and the `ri` global)
 * is already provided by files/common.c, common/shared/shared.c and
 * gl3_main.c.
 *
 * This file supplies only the genuinely missing host pieces:
 *   - Com_BlockChecksum()  (deterministic 32-bit CRC of a memory block)
 *   - Hunk_Begin/Alloc/End/Free()  (bump allocator used by model/lightmap load)
 *
 * It must be self-contained against libc only: soft3d apps link no BSP/system
 * glue beyond EWOK_LIBC/libm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"   /* byte, qboolean, Com_Error, Hunk_* prototypes (shared.h) */

/* ------------------------------------------------------------------ *
 * Com_BlockChecksum
 *
 * Upstream uses this for map/model integrity only; the value never leaves
 * the process in a refresher-only build, so any deterministic 32-bit hash
 * is sufficient.  Standard CRC-32 (IEEE 802.3 polynomial, reflected).
 * ------------------------------------------------------------------ */
unsigned
Com_BlockChecksum(const void *buffer, int length)
{
	static unsigned table[256];
	static int table_ready = 0;
	const unsigned char *p = (const unsigned char *)buffer;
	unsigned crc = 0xFFFFFFFFu;
	int i;

	if (!table_ready)
	{
		unsigned n, k;
		for (n = 0; n < 256; ++n)
		{
			unsigned c = n;
			for (k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[n] = c;
		}
		table_ready = 1;
	}

	for (i = 0; i < length; ++i)
		crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);

	return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ *
 * Hunk allocator
 *
 * yquake2 semantics: Hunk_Begin(maxsize) opens a region, Hunk_Alloc() bump
 * allocates zeroed memory inside it, Hunk_End() closes it and returns the
 * used byte count, Hunk_Free(buf) releases the region.  Only one hunk is
 * live at a time in the refresher (model + lightmap loading), but we track
 * the buffer header so Hunk_Free() can release whichever block it is handed.
 * ------------------------------------------------------------------ */
typedef struct hunk_s
{
	int             maxsize;
	int             used;      /* bytes consumed after the header */
	struct hunk_s  *next;      /* so Hunk_Free can find the header from a payload ptr */
} hunk_t;

static hunk_t *g_curhunk = NULL;

#define HUNK_ALIGN 16

void *
Hunk_Begin(int maxsize)
{
	hunk_t *h;
	int cap;

	if (maxsize <= 0)
		maxsize = 1;

	/* Hunk_Alloc() rounds every request up to HUNK_ALIGN, so a region sized
	 * exactly to maxsize overflows on a single allocation whose size is already
	 * a multiple of the file length (e.g. an MD2 whose ofs_end == modfilelen:
	 * ALIGN(824) = 832 > 824).  Round the region up to the granularity and add
	 * one extra HUNK_ALIGN of slack so the aligned bump always fits. */
	cap = ((maxsize + HUNK_ALIGN - 1) & ~(HUNK_ALIGN - 1)) + HUNK_ALIGN;

	h = (hunk_t *)calloc(1, sizeof(hunk_t) + (size_t)cap);
	if (!h)
		Com_Error(ERR_FATAL, "Hunk_Begin: calloc(%d) failed", maxsize);

	h->maxsize = cap;
	h->used = 0;
	h->next = g_curhunk;
	g_curhunk = h;

	return (void *)(h + 1);
}

void *
Hunk_Alloc(int size)
{
	hunk_t *h = g_curhunk;
	void *base, *ptr;
	int aligned;

	if (!h)
		Com_Error(ERR_FATAL, "Hunk_Alloc without Hunk_Begin");
	if (size < 0)
		size = 0;

	aligned = (size + HUNK_ALIGN - 1) & ~(HUNK_ALIGN - 1);
	if (h->used + aligned > h->maxsize)
		Com_Error(ERR_FATAL, "Hunk_Alloc: overflow (%d + %d > %d)",
		          h->used, aligned, h->maxsize);

	base = (void *)(h + 1);
	ptr = (char *)base + h->used;
	h->used += aligned;
	memset(ptr, 0, (size_t)size);

	return ptr;
}

int
Hunk_End(void)
{
	hunk_t *h = g_curhunk;
	int used;

	if (!h)
		return 0;
	used = h->used;
	g_curhunk = h->next;   /* close: the block stays alive until Hunk_Free */
	return used;
}

void
Hunk_Free(void *buf)
{
	if (!buf)
		return;

	/* The caller hands back the payload pointer returned by Hunk_Begin, so
	 * the header sits immediately before it. */
	hunk_t *h = ((hunk_t *)buf) - 1;

	/* Unlink from the live chain if still present. */
	if (g_curhunk == h)
	{
		g_curhunk = h->next;
	}
	free(h);
}
