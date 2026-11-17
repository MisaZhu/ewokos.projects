#ifndef WECHAT_CONTACT_LIST_HH
#define WECHAT_CONTACT_LIST_HH

#include <Widget/List.h>
#include "Backend.h"
#include "AvatarCache.h"
#include "SessionList.h"

using namespace Ewok;

/* Contacts tab: avatar + display name rows; clicking opens a chat. */
class ContactList : public List {
	Backend* backend;
	AvatarCache* avatars;
	SessionOpenFunc openFunc;
	void* openArg;
protected:
	void drawItem(graph_t* g, XTheme* theme, int32_t index, const grect_t& r) override;
	void onSelect(int sel) override;
public:
	ContactList(Backend* b, AvatarCache* a);
	void setOpenFunc(SessionOpenFunc f, void* arg);
	void refresh();
};

#endif