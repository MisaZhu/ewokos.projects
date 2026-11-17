#include <Widget/WidgetWin.h>
#include <Widget/WidgetX.h>
#include <Widget/RootWidget.h>
#include <Widget/Container.h>
#include <Widget/EditLine.h>
#include <Widget/LabelButton.h>
#include <Widget/Scroller.h>
#include <x++/X.h>
#include <x++/XTheme.h>
#include <ewoksys/keydef.h>
#include <mouse/mouse.h>
#include <unistd.h>

#include "Backend.h"
#include "LocalBackend.h"
#include "AvatarCache.h"
#include "LoginWidget.h"
#include "SessionList.h"
#include "ContactList.h"
#include "ChatWidget.h"
#include "ImageViewWin.h"
#include "Util.h"

using namespace Ewok;

#define WX_RAIL_BG    0xFF2E2E2E
#define WX_GREEN      0xFF07C160
#define WX_ICON_GRAY  0xFF8A8A8A
#define WX_CHAT_BG    0xFFF5F5F5
#define WX_TEXT       0xFF191919
#define WX_LINE       0xFFE0E0E0

class WeChatWin;

/* Left navigation rail: self avatar, chat/contacts tabs, logout. Icons are
 * composed from graph primitives (libgraph has no polygon fill). */
class RailWidget : public Widget {
public:
	typedef void (*TabFunc)(int tab, void* arg);
private:
	AvatarCache* avatars;
	Backend* backend;
	TabFunc tabFunc;
	void* tabArg;
	int tab;
	grect_t chatRect, contactRect, logoutRect;
protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) override;
	bool onMouse(xevent_t* ev) override;
public:
	RailWidget(Backend* b, AvatarCache* a);
	void setTabFunc(TabFunc f, void* arg) { tabFunc = f; tabArg = arg; }
	void setTab(int t);
};

/* Chat title bar */
class ChatHeader : public Widget {
	string title;
protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) override {
		graph_fill_rect(g, r.x, r.y, r.w, r.h, WX_CHAT_BG);
		graph_fill_rect(g, r.x, r.y + r.h - 1, r.w, 1, WX_LINE);
		font_t* font = theme->getFont();
		if(font == NULL || title.empty())
			return;
		graph_draw_text_font_align(g, r.x + X::hidpiSize(16), r.y,
				r.w - X::hidpiSize(32), r.h, title.c_str(), font,
				theme->basic.fontSize + 2, WX_TEXT, FONT_ALIGN_NONE);
	}
public:
	ChatHeader() { alpha = false; }
	void setTitle(const string& t) { title = t; update(); }
};

/* EditLine with UTF-8 safe editing and Enter-to-send. The stock EditLine
 * backspace removes one byte, which corrupts CJK characters. */
class ChatEditLine : public EditLine {
	typedef void (*SendFunc)(void* arg);
	SendFunc sendFunc;
	void* sendArg;
protected:
	bool onIM(xevent_t* ev) override {
		if(ev->state != XIM_STATE_PRESS)
			return false;
		uint32_t v = ev->value.im.value;
		if(v == KEY_ENTER) {
			if(sendFunc != NULL)
				sendFunc(sendArg);
			return true;
		}
		if(v == KEY_BACKSPACE || v == CONSOLE_LEFT) {
			utf8PopBack(content);
		}
		else if(v > 27 && v != KEY_LSHIFT && v != KEY_RSHIFT && v != KEY_CTRL) {
			utf8Append(content, v);
		}
		onInput(v);
		update();
		return true;
	}
public:
	ChatEditLine() { sendFunc = NULL; sendArg = NULL; }
	void setSendFunc(SendFunc f, void* arg) { sendFunc = f; sendArg = arg; }
};

class WeChatWin : public WidgetWin {
	LocalBackend backend;
	AvatarCache avatars;

	LoginWidget* login;
	Container* mainC;
	RailWidget* rail;
	Container* sessionPane;
	Container* contactPane;
	SessionList* sessions;
	ContactList* contacts;
	ChatWidget* chat;
	ChatHeader* header;
	ChatEditLine* input;
	ImageViewWin* viewer;

	void openSession(const string& talker) {
		WSession* s = backend.session(talker, true);
		if(s == NULL)
			return;
		s->unread = 0;
		chat->setTalker(talker);
		chat->refresh(false);
		const WContact* c = backend.contact(talker);
		header->setTitle(c != NULL ? c->displayName() : talker);
		sessions->setCurrent(talker);
		sessions->refresh();
		showTab(0);
	}

	void sendCurrent() {
		string text = input->getContent();
		/* trim */
		size_t b = text.find_first_not_of(" \t\r\n");
		if(b == string::npos || chat->getTalker().empty())
			return;
		size_t e = text.find_last_not_of(" \t\r\n");
		text = text.substr(b, e - b + 1);

		WMessage m;
		m.type = "text";
		m.text = text;
		backend.send(chat->getTalker(), m);
		input->setContent("");
		chat->refresh(false);
		sessions->refresh();
	}

	void openImage(const string& resFile) {
		if(viewer != NULL) {
			viewer->close();
			delete viewer;
			viewer = NULL;
		}
		viewer = new ImageViewWin(resFile);
		if(!viewer->valid()) {
			delete viewer;
			viewer = NULL;
			return;
		}
		int32_t w = viewer->imgW();
		int32_t h = viewer->imgH();
		int32_t maxW = X::hidpiSize(720);
		int32_t maxH = X::hidpiSize(540);
		if(w > maxW || h > maxH) {
			float scale = (float)maxW / (float)w;
			float sy = (float)maxH / (float)h;
			if(sy < scale)
				scale = sy;
			w = (int32_t)(w * scale);
			h = (int32_t)(h * scale);
		}
		if(w < X::hidpiSize(160)) w = X::hidpiSize(160);
		if(h < X::hidpiSize(120)) h = X::hidpiSize(120);
		viewer->open(getX(), -1, -1, -1, w, h, "图片", XWIN_STYLE_NORMAL);
	}

	void doLogin() {
		backend.login();
		login->hide();
		mainC->show();
		sessions->refresh();
		contacts->refresh();
		vector<WSession>& ss = backend.sessions();
		if(!ss.empty())
			openSession(ss[0].talker);
	}

	void doLogout() {
		backend.logout();
		chat->setTalker("");
		header->setTitle("");
		sessions->setCurrent("");
		mainC->hide();
		login->show();
	}

	static void onLoginClick(Widget* wd, xevent_t* ev, void* arg) {
		(void)wd; (void)ev;
		((WeChatWin*)arg)->doLogin();
	}
	static void onSessionOpen(const string& talker, void* arg) {
		((WeChatWin*)arg)->openSession(talker);
	}
	static void onImageOpen(const string& resFile, void* arg) {
		((WeChatWin*)arg)->openImage(resFile);
	}
	static void onSendClick(Widget* wd, xevent_t* ev, void* arg) {
		(void)wd;
		if(ev->type != XEVT_MOUSE || ev->state != MOUSE_STATE_CLICK)
			return;
		((WeChatWin*)arg)->sendCurrent();
	}
	static void onSendEnter(void* arg) {
		((WeChatWin*)arg)->sendCurrent();
	}
	static void onRailTab(int tab, void* arg) {
		WeChatWin* win = (WeChatWin*)arg;
		if(tab == 2)
			win->doLogout();
		else
			win->showTab(tab);
	}
protected:
	void onTimer(uint32_t timerFPS, uint32_t timerSteps) override {
		(void)timerFPS; (void)timerSteps;
		if(backend.loggedIn() && backend.tick()) {
			/* a simulated reply landed: refresh lists; the open session
			 * is being read, so it stays without unread badge */
			WSession* cur = backend.session(chat->getTalker(), false);
			if(cur != NULL)
				cur->unread = 0;
			sessions->refresh();
			if(!chat->getTalker().empty())
				chat->refresh(true);
		}
		if(viewer != NULL && viewer->isClosed()) {
			delete viewer;
			viewer = NULL;
		}
	}
public:
	WeChatWin() {
		viewer = NULL;
	}
	~WeChatWin() {
		if(viewer != NULL) {
			viewer->close();
			delete viewer;
		}
	}

	void showTab(int tab) {
		rail->setTab(tab);
		if(tab == 0) {
			sessionPane->show();
			contactPane->hide();
		}
		else {
			sessionPane->hide();
			contactPane->show();
		}
	}

	void buildUI() {
		RootWidget* root = new RootWidget();
		setRoot(root);
		root->setType(Container::OVERLAP);

		login = new LoginWidget();
		login->setEventFunc(onLoginClick, this);
		root->add(login);

		mainC = new Container();
		mainC->setType(Container::HORIZONTAL);
		root->add(mainC);

		rail = new RailWidget(&backend, &avatars);
		rail->setTabFunc(onRailTab, this);
		rail->fix(X::hidpiSize(56), 0);
		mainC->add(rail);

		/* middle pane: sessions / contacts stacked, toggled by the rail */
		Container* listPane = new Container();
		listPane->setType(Container::OVERLAP);
		listPane->fix(X::hidpiSize(260), 0);
		mainC->add(listPane);

		sessionPane = new Container();
		sessionPane->setType(Container::HORIZONTAL);
		listPane->add(sessionPane);
		sessions = new SessionList(&backend, &avatars);
		sessions->setOpenFunc(onSessionOpen, this);
		sessionPane->add(sessions);
		Scroller* sr1 = new Scroller();
		sr1->fix(X::hidpiSize(8), 0);
		sessions->setScrollerV(sr1);
		sessionPane->add(sr1);

		contactPane = new Container();
		contactPane->setType(Container::HORIZONTAL);
		listPane->add(contactPane);
		contacts = new ContactList(&backend, &avatars);
		contacts->setOpenFunc(onSessionOpen, this);
		contactPane->add(contacts);
		Scroller* sr2 = new Scroller();
		sr2->fix(X::hidpiSize(8), 0);
		contacts->setScrollerV(sr2);
		contactPane->add(sr2);
		contactPane->hide();

		/* right pane: header + messages + input */
		Container* chatPane = new Container();
		chatPane->setType(Container::VERTICAL);
		mainC->add(chatPane);

		header = new ChatHeader();
		header->fix(0, X::hidpiSize(48));
		chatPane->add(header);

		chat = new ChatWidget(&backend, &avatars);
		chat->setImageOpenFunc(onImageOpen, this);
		chatPane->add(chat);

		Container* inputBar = new Container();
		inputBar->setType(Container::HORIZONTAL);
		inputBar->fix(0, X::hidpiSize(52));
		chatPane->add(inputBar);

		input = new ChatEditLine();
		input->setSendFunc(onSendEnter, this);
		input->setMarginH(X::hidpiSize(8));
		inputBar->add(input);

		LabelButton* sendBtn = new LabelButton("发送");
		sendBtn->fix(X::hidpiSize(72), 0);
		sendBtn->setMarginH(X::hidpiSize(8));
		sendBtn->setEventFunc(onSendClick, this);
		inputBar->add(sendBtn);

		mainC->hide();
		sessions->refresh();
		contacts->refresh();
	}
};

/* ---- RailWidget implementation (needs WeChatWin complete) ---- */

RailWidget::RailWidget(Backend* b, AvatarCache* a) {
	backend = b;
	avatars = a;
	tabFunc = NULL;
	tabArg = NULL;
	tab = 0;
	alpha = false;
	memset(&chatRect, 0, sizeof(chatRect));
	memset(&contactRect, 0, sizeof(contactRect));
	memset(&logoutRect, 0, sizeof(logoutRect));
}

void RailWidget::setTab(int t) {
	tab = t;
	update();
}

void RailWidget::onRepaint(graph_t* g, XTheme* theme, const grect_t& r) {
	graph_fill_rect(g, r.x, r.y, r.w, r.h, WX_RAIL_BG);
	font_t* font = theme->getFont();
	uint32_t fs = theme->basic.fontSize;

	int32_t av = X::hidpiSize(36);
	avatars->draw(g, &backend->self(), r.x + (r.w - av)/2,
			r.y + X::hidpiSize(16), av, font, fs);

	int32_t bw = r.w;
	int32_t bh = X::hidpiSize(44);
	chatRect.x = 0; chatRect.y = X::hidpiSize(72);
	chatRect.w = bw; chatRect.h = bh;
	contactRect.x = 0; contactRect.y = X::hidpiSize(120);
	contactRect.w = bw; contactRect.h = bh;

	/* chat bubble icon */
	uint32_t c0 = (tab == 0) ? WX_GREEN : WX_ICON_GRAY;
	int32_t cx = r.x + r.w/2;
	int32_t cy = r.y + chatRect.y + bh/2;
	graph_fill_round(g, cx - X::hidpiSize(13), cy - X::hidpiSize(10),
			X::hidpiSize(26), X::hidpiSize(19), X::hidpiSize(6), c0);
	graph_fill_triangle(g,
			cx - X::hidpiSize(6), cy + X::hidpiSize(8),
			cx - X::hidpiSize(1), cy + X::hidpiSize(8),
			cx - X::hidpiSize(8), cy + X::hidpiSize(13), c0);

	/* contact icon: head + shoulders */
	uint32_t c1 = (tab == 1) ? WX_GREEN : WX_ICON_GRAY;
	cy = r.y + contactRect.y + bh/2;
	graph_fill_circle(g, cx, cy - X::hidpiSize(5), X::hidpiSize(6), c1);
	graph_fill_round(g, cx - X::hidpiSize(11), cy + X::hidpiSize(3),
			X::hidpiSize(22), X::hidpiSize(10), X::hidpiSize(5), c1);

	/* logout at the bottom */
	int32_t lh = X::hidpiSize(32);
	logoutRect.x = 0; logoutRect.y = r.h - lh;
	logoutRect.w = bw; logoutRect.h = lh;
	if(font != NULL) {
		graph_draw_text_font_align(g, r.x, r.y + logoutRect.y, r.w, lh,
				"退出", font, fs - 2, WX_ICON_GRAY, FONT_ALIGN_CENTER);
	}
}

bool RailWidget::onMouse(xevent_t* ev) {
	if(ev->state != MOUSE_STATE_CLICK)
		return true;
	gpos_t p = getInsidePos(ev->value.mouse.x, ev->value.mouse.y);
	int t = -1;
	if(p.y >= chatRect.y && p.y < chatRect.y + chatRect.h)
		t = 0;
	else if(p.y >= contactRect.y && p.y < contactRect.y + contactRect.h)
		t = 1;
	else if(p.y >= logoutRect.y && p.y < logoutRect.y + logoutRect.h)
		t = 2;
	if(t >= 0 && tabFunc != NULL)
		tabFunc(t, tabArg);
	return true;
}

int main(int argc, char** argv) {
	(void)argc; (void)argv;
	X x;
	WeChatWin win;
	win.buildUI();
	win.open(&x, -1, -1, -1, X::hidpiSize(860), X::hidpiSize(600),
			"微信", XWIN_STYLE_NORMAL);
	/* CJK font for contact names and chat text */
	win.getTheme()->setFont("system-cn", 14);
	win.setTimer(20);
	widgetXRun(&x, &win);
	return 0;
}