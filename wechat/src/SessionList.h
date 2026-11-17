#ifndef WECHAT_SESSION_LIST_HH
#define WECHAT_SESSION_LIST_HH

#include <Widget/List.h>
#include "Backend.h"
#include "AvatarCache.h"

using namespace Ewok;

typedef void (*SessionOpenFunc)(const string& talker, void* arg);

/* Middle-pane recent chat list: avatar + name + last message + time +
 * unread badge, WeChat PC style. */
class SessionList : public List {
	Backend* backend;
	AvatarCache* avatars;
	SessionOpenFunc openFunc;
	void* openArg;
	string current;
protected:
	void drawItem(graph_t* g, XTheme* theme, int32_t index, const grect_t& r) override;
	void onSelect(int sel) override;
public:
	SessionList(Backend* b, AvatarCache* a);
	void setOpenFunc(SessionOpenFunc f, void* arg);
	void setCurrent(const string& talker);
	void refresh();
};

#endif