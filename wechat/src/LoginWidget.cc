#include "LoginWidget.h"
#include <x++/X.h>
#include <x++/XTheme.h>
#include <mouse/mouse.h>

using namespace Ewok;

#define WX_GREEN      0xFF07C160
#define WX_GREEN_DARK 0xFF06AD56
#define WX_CARD_BG    0xFFFFFFFF
#define WX_TEXT       0xFF191919
#define WX_TEXT_GRAY  0xFF888888

LoginWidget::LoginWidget() {
	alpha = false;
	scanY = 0;
	scanDir = 1;
	hovering = false;
	memset(&btnRect, 0, sizeof(btnRect));
}

void LoginWidget::drawQR(graph_t* g, int32_t x, int32_t y, int32_t size) {
	const int N = 25;
	int32_t cell = size / N;
	int32_t off = (size - cell*N)/2;
	x += off;
	y += off;

	graph_fill_rect(g, x - 4, y - 4, cell*N + 8, cell*N + 8, 0xFFFFFFFF);
	for(int j = 0; j < N; j++) {
		for(int i = 0; i < N; i++) {
			/* finder patterns in three corners */
			bool finder = (i < 7 && j < 7) || (i >= N-7 && j < 7) || (i < 7 && j >= N-7);
			if(finder)
				continue;
			uint32_t h = (uint32_t)(i*73856093u) ^ (uint32_t)(j*19349663u);
			h = (h ^ (h >> 13)) * 1274126177u;
			if((h >> 8) % 5 < 2)
				graph_fill_rect(g, x + i*cell, y + j*cell, cell, cell, 0xFF191919);
		}
	}

	/* finder: dark 7x7, white 5x5, dark 3x3 */
	int fx[3] = {0, N-7, 0};
	int fy[3] = {0, 0, N-7};
	for(int k = 0; k < 3; k++) {
		int px = x + fx[k]*cell, py = y + fy[k]*cell;
		graph_fill_rect(g, px, py, cell*7, cell*7, 0xFF191919);
		graph_fill_rect(g, px+cell, py+cell, cell*5, cell*5, 0xFFFFFFFF);
		graph_fill_rect(g, px+cell*2, py+cell*2, cell*3, cell*3, 0xFF191919);
	}

	/* animated scan line */
	int32_t sy = y + scanY % (cell*N);
	graph_fill_rect(g, x, sy, cell*N, 3, 0xC007C160);
}

void LoginWidget::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	font_t* font = theme->getFont();
	uint32_t fs = theme->basic.fontSize;

	graph_fill_rect(g, r.x, r.y, r.w, r.h, WX_GREEN);
	if(font == NULL)
		return;

	int32_t cw = X::hidpiSize(320);
	if(cw > r.w - X::hidpiSize(24))
		cw = r.w - X::hidpiSize(24);
	int32_t ch = X::hidpiSize(420);
	if(ch > r.h - X::hidpiSize(24))
		ch = r.h - X::hidpiSize(24);
	int32_t cx = r.x + (r.w - cw)/2;
	int32_t cy = r.y + (r.h - ch)/2;

	graph_fill_round(g, cx, cy, cw, ch, X::hidpiSize(8), WX_CARD_BG);

	/* title */
	graph_draw_text_font_align(g, cx, cy + X::hidpiSize(20), cw, X::hidpiSize(30),
			"微信", font, fs + 8, WX_TEXT, FONT_ALIGN_CENTER);

	/* QR */
	int32_t qs = X::hidpiSize(180);
	if(qs > cw - X::hidpiSize(48))
		qs = cw - X::hidpiSize(48);
	int32_t qx = cx + (cw - qs)/2;
	int32_t qy = cy + X::hidpiSize(64);
	drawQR(g, qx, qy, qs);

	/* captions */
	int32_t ty = qy + qs + X::hidpiSize(20);
	graph_draw_text_font_align(g, cx, ty, cw, X::hidpiSize(20),
			"用手机微信扫码登录", font, fs, WX_TEXT, FONT_ALIGN_CENTER);
	ty += X::hidpiSize(24);
	graph_draw_text_font_align(g, cx, ty, cw, X::hidpiSize(18),
			"登录成功后，手机微信保持在线", font, fs - 2, WX_TEXT_GRAY, FONT_ALIGN_CENTER);

	/* confirm button (simulates the phone-side approval) */
	int32_t bw = cw - X::hidpiSize(48);
	int32_t bh = X::hidpiSize(38);
	int32_t bx = cx + (cw - bw)/2;
	int32_t by = cy + ch - bh - X::hidpiSize(24);
	btnRect.x = bx - r.x;
	btnRect.y = by - r.y;
	btnRect.w = bw;
	btnRect.h = bh;
	graph_fill_round(g, bx, by, bw, bh, X::hidpiSize(6),
			hovering ? WX_GREEN_DARK : WX_GREEN);
	graph_draw_text_font_align(g, bx, by, bw, bh,
			"模拟扫码确认登录", font, fs, 0xFFFFFFFF, FONT_ALIGN_CENTER);
}

bool LoginWidget::onMouse(xevent_t* ev) {
	gpos_t p = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);
	bool in = p.x >= btnRect.x && p.x < btnRect.x + btnRect.w &&
		p.y >= btnRect.y && p.y < btnRect.y + btnRect.h;

	if(ev->state == MOUSE_STATE_MOVE) {
		if(in != hovering) {
			hovering = in;
			update();
		}
		return true;
	}
	if(ev->state == MOUSE_STATE_CLICK && in) {
		if(onEventFunc != NULL)
			onEventFunc(this, ev, onEventFuncArg);
		return true;
	}
	return true;
}

void LoginWidget::onTimer(uint32_t timerFPS, uint32_t timerSteps) {
	(void)timerFPS;
	(void)timerSteps;
	scanY += scanDir * X::hidpiSize(3);
	if(scanY > X::hidpiSize(180) || scanY < 0)
		scanDir = -scanDir;
	update();
}