#ifndef WECHAT_CHAT_WIDGET_HH
#define WECHAT_CHAT_WIDGET_HH

#include <Widget/Widget.h>
#include "Backend.h"
#include "AvatarCache.h"
#include <map>

using namespace Ewok;

typedef void (*ImageOpenFunc)(const string& resFile, void* arg);

/* Chat pane: self-laid-out bubble list with pixel scrolling. Text bubbles
 * wrap CJK text per codepoint; image messages show a thumbnail that opens
 * the full-size viewer on click. */
class ChatWidget : public Widget {
public:
	struct ItemLayout {
		int32_t y, h;
		int32_t sepH;
		string timeStr;
		bool isImage;
		bool fromMe;
		grect_t bubble;
		grect_t imgRect;
		vector<string> lines;
	};
private:
	Backend* backend;
	AvatarCache* avatars;
	ImageOpenFunc imageFunc;
	void* imageArg;
	string talker;

	map<string, graph_t*> imgCache;
	vector<ItemLayout> layout;
	int32_t contentH;
	int32_t scrollY;
	bool stickBottom;
	bool dragging;
	bool layoutDirty;

	graph_t* getImage(const string& resFile);
	void relayout(XTheme* theme);
	int32_t viewH() const;
	int32_t maxScroll() const;
	int32_t baseOffset() const;
	void clampScroll();
	void wrapText(const string& text, font_t* font, uint32_t fs,
			int32_t maxW, vector<string>& lines);
protected:
	void onRepaint(graph_t* g, XTheme* theme, const grect_t& r) override;
	bool onMouse(xevent_t* ev) override;
	void onResize() override;
public:
	ChatWidget(Backend* b, AvatarCache* a);
	~ChatWidget();
	void setImageOpenFunc(ImageOpenFunc f, void* arg);
	void setTalker(const string& id);
	const string& getTalker() { return talker; }
	void refresh(bool keepPosition);
};

#endif