#ifndef WECHAT_LOGIN_WIDGET_HH
#define WECHAT_LOGIN_WIDGET_HH

#include <Widget/Widget.h>

using namespace Ewok;

/* WeChat-style login page: green backdrop, QR card with an animated scan
 * line. The demo backend has no real QR exchange, so a confirm button
 * simulates the phone-side scan approval (phone stays online). */
class LoginWidget : public Widget {
	grect_t btnRect;
	int32_t scanY;
	int32_t scanDir;
	bool hovering;

	void drawQR(graph_t* g, int32_t x, int32_t y, int32_t size);
protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) override;
	bool onMouse(xevent_t* ev) override;
	void onTimer(uint32_t timerFPS, uint32_t timerSteps) override;
public:
	LoginWidget();
};

#endif