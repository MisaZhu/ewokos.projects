#include "ImageViewWin.h"
#include <x++/X.h>
#include <graph/graph_image.h>

using namespace Ewok;

ImageViewWin::ImageViewWin(const string& resFile) {
	img = NULL;
	closedFlag = false;
	string fname = X::getResFullName(resFile.c_str());
	img = graph_image_new(fname.c_str());
}

ImageViewWin::~ImageViewWin() {
	if(img != NULL)
		graph_free(img);
}

bool ImageViewWin::onClose() {
	closedFlag = true;
	return true;
}

void ImageViewWin::onRepaint(graph_t* g) {
	graph_fill_rect(g, 0, 0, g->w, g->h, 0xFF191919);
	if(img == NULL || img->w <= 0 || img->h <= 0)
		return;

	/* aspect-fit into the window canvas */
	float scale = (float)g->w / (float)img->w;
	float sy = (float)g->h / (float)img->h;
	if(sy < scale)
		scale = sy;
	if(scale > 1.0f)
		scale = 1.0f;
	int32_t dw = (int32_t)(img->w * scale);
	int32_t dh = (int32_t)(img->h * scale);
	if(dw < 1) dw = 1;
	if(dh < 1) dh = 1;
	graph_blt_fit(img, 0, 0, img->w, img->h,
			g, (g->w - dw)/2, (g->h - dh)/2, dw, dh);
}