#ifndef WECHAT_LOCAL_BACKEND_HH
#define WECHAT_LOCAL_BACKEND_HH

#include "Backend.h"

/* Self-contained backend: loads demo contacts/history from res/data.json and
 * simulates replies locally, so the whole login -> contacts -> chat -> image
 * flow runs on EwokOS alone with no host service. */
class LocalBackend : public Backend {
	bool logged;
	WContact me;
	vector<WContact> contactList;
	vector<WSession> sessionList;
	int64_t nextMsgId;

	struct PendingReply {
		string talker;
		string text;
		uint64_t dueTic;
	};
	vector<PendingReply> replies;

	void load();
	void scheduleReply(const string& talker, const string& userText);
	void touchSession(WSession* s);
	string replyFor(const string& userText);
public:
	LocalBackend();
	~LocalBackend();

	bool loggedIn() override { return logged; }
	void login() override;
	void logout() override;
	const WContact& self() override { return me; }

	const vector<WContact>& contacts() override { return contactList; }
	const WContact* contact(const string& id) override;

	vector<WSession>& sessions() override { return sessionList; }
	WSession* session(const string& talker, bool create) override;

	int64_t send(const string& talker, const WMessage& msg) override;
	bool tick() override;
};

#endif