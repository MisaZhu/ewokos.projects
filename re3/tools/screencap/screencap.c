// Dump the composed screen (via the x window server's shared screen graph)
// to a 32bpp BMP. Debug helper for the re3 port: run on the board, then
// fetch the BMP over sftp and view it on the host.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <x/x.h>

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

int main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "/tmp/screen.bmp";

	x_t x;
	x_init(&x, NULL);

	graph_t g;
	memset(&g, 0, sizeof(g));
	if (x_fetch_screen_graph(0, &g) != 0 || g.buffer == NULL) {
		printf("screencap: x_fetch_screen_graph failed\n");
		return 1;
	}

	int w = g.w;
	int h = g.h;
	uint32_t row_bytes = (uint32_t)w * 4;
	uint32_t data_size = row_bytes * (uint32_t)h;

	uint8_t header[54];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	put32(header + 2, 54 + data_size);
	put32(header + 10, 54);
	put32(header + 14, 40);
	put32(header + 18, (uint32_t)w);
	put32(header + 22, (uint32_t)h);
	header[26] = 1;
	header[28] = 32;
	put32(header + 34, data_size);

	FILE *fp = fopen(out, "wb");
	if (fp == NULL) {
		printf("screencap: cannot open %s\n", out);
		return 1;
	}
	fwrite(header, 1, sizeof(header), fp);
	for (int y = h - 1; y >= 0; y--)
		fwrite(g.buffer + (size_t)y * w, 1, row_bytes, fp);
	fclose(fp);

	printf("screencap: wrote %s (%dx%d)\n", out, w, h);
	return 0;
}
