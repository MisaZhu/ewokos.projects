#ifndef WECHAT_IMAGE_VIEW_WIN_HH
#define WECHAT_IMAGE_VIEW_WIN_HH

#include <Widget/WidgetWin.h>
#include <string>

using namespace Ewok;
using namespace std;

/* Full-size image viewer popup. Owned by the main window, which deletes it
 * once the user closes it (isClosed()). */
class ImageViewWin : public WidgetWin {
	graph_t* img;
	bool closedFlag;
protected:
	void onRepaint(graph_t* g) override;
	bool onClose() override;
public:
	ImageViewWin(const string& resFile);
	~ImageViewWin();
	bool isClosed() { return closedFlag; }
	bool valid() { return img != NULL; }
	int32_t imgW() { return img != NULL ? img->w : 0; }
	int32_t imgH() { return img != NULL ? img->h : 0; }
};

#endif