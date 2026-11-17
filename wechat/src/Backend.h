#ifndef WECHAT_BACKEND_HH
#define WECHAT_BACKEND_HH

#include <string>
#include <vector>
#include <stdint.h>

using namespace std;

/* Pluggable backend interface. The app ships with LocalBackend (self-contained
 * demo data, no host dependency); a future backend can talk to a real WeChat
 * session without touching the UI layer. */

struct WContact {
	string id;
	string nickname;
	string remark;
	string avatar;   /* res file name, empty => drawn initial avatar */
	uint32_t color;  /* fallback avatar color */

	string displayName() const {
		return remark.empty() ? nickname : remark;
	}
};

struct WMessage {
	int64_t id;
	string type;     /* "text" | "image" */
	string text;     /* text body, or image res file name for "image" */
	string sender;   /* contact id, or "me" */
	uint64_t time;   /* unix seconds */
	bool pending;    /* local send not yet acknowledged */

	WMessage() : id(0), time(0), pending(false) { }
};

struct WSession {
	string talker;   /* contact id */
	vector<WMessage> messages;
	uint32_t unread;

	WSession() : unread(0) { }
};

class Backend {
public:
	virtual ~Backend() { }

	virtual bool loggedIn() = 0;
	virtual void login() = 0;
	virtual void logout() = 0;
	virtual const WContact& self() = 0;

	virtual const vector<WContact>& contacts() = 0;
	virtual const WContact* contact(const string& id) = 0;

	virtual vector<WSession>& sessions() = 0;
	virtual WSession* session(const string& talker, bool create) = 0;

	/* append an outgoing message; returns its id */
	virtual int64_t send(const string& talker, const WMessage& msg) = 0;

	/* move due simulated replies into their sessions; returns true if
	 * anything changed so the UI can refresh */
	virtual bool tick() = 0;
};

#endif