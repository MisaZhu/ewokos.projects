#include "LocalBackend.h"
#include "Util.h"
#include <x++/X.h>
#include <tinyjson/tinyjson.h>
#include <ewoksys/kernel_tic.h>
#include <time.h>
#include <string.h>

using namespace Ewok;

LocalBackend::LocalBackend() {
	logged = false;
	nextMsgId = 1;
	load();
}

LocalBackend::~LocalBackend() {
}

static uint32_t parseColor(const char* s, uint32_t def) {
	if(s == NULL || s[0] != '#')
		return def;
	uint32_t v = 0;
	for(int i = 1; s[i] != 0 && i <= 6; i++) {
		char c = s[i];
		v <<= 4;
		if(c >= '0' && c <= '9') v |= c - '0';
		else if(c >= 'a' && c <= 'f') v |= c - 'a' + 10;
		else if(c >= 'A' && c <= 'F') v |= c - 'A' + 10;
	}
	return 0xFF000000 | v;
}

static void loadContact(json_var_t* obj, WContact& c) {
	c.id = json_get_str_def(obj, "id", "");
	c.nickname = json_get_str_def(obj, "nickname", "");
	c.remark = json_get_str_def(obj, "remark", "");
	c.avatar = json_get_str_def(obj, "avatar", "");
	c.color = parseColor(json_get_str_def(obj, "color", ""), 0xFF4A90D9);
}

void LocalBackend::load() {
	string fname = X::getResFullName("data.json");
	json_var_t* data = json_parse_file(fname.c_str());
	if(data == NULL)
		return;

	json_var_t* selfObj = json_get_obj(data, "self");
	if(selfObj != NULL)
		loadContact(selfObj, me);
	if(me.id.empty())
		me.id = "me";

	json_var_t* contacts = json_get_obj(data, "contacts");
	if(contacts != NULL) {
		uint32_t n = json_var_array_size(contacts);
		for(uint32_t i = 0; i < n; i++) {
			json_var_t* it = json_var_array_get_var(contacts, i);
			WContact c;
			loadContact(it, c);
			if(!c.id.empty())
				contactList.push_back(c);
		}
	}

	uint64_t now = (uint64_t)time(NULL);
	json_var_t* sessions = json_get_obj(data, "sessions");
	if(sessions != NULL) {
		uint32_t n = json_var_array_size(sessions);
		for(uint32_t i = 0; i < n; i++) {
			json_var_t* it = json_var_array_get_var(sessions, i);
			WSession s;
			s.talker = json_get_str_def(it, "talker", "");
			s.unread = json_get_int_def(it, "unread", 0);
			json_var_t* msgs = json_get_obj(it, "messages");
			if(msgs != NULL) {
				uint32_t mn = json_var_array_size(msgs);
				for(uint32_t j = 0; j < mn; j++) {
					json_var_t* mv = json_var_array_get_var(msgs, j);
					WMessage m;
					m.id = nextMsgId++;
					m.type = json_get_str_def(mv, "type", "text");
					m.text = json_get_str_def(mv, "text", "");
					m.sender = json_get_str_def(mv, "sender", s.talker.c_str());
					int ago = json_get_int_def(mv, "ago", 0);
					m.time = now - ago;
					s.messages.push_back(m);
				}
			}
			if(!s.talker.empty())
				sessionList.push_back(s);
		}
	}
	json_var_unref(data);
}

void LocalBackend::login() {
	logged = true;
	/* one ambient incoming message shortly after login, to show the unread
	 * badge / session reorder behavior without any user action */
	PendingReply r;
	r.talker = "file_helper";
	r.text = "（演示）这是一条新消息，用于展示未读提醒。";
	r.dueTic = kernel_tic_ms(0) + 10000;
	replies.push_back(r);
}

void LocalBackend::logout() {
	logged = false;
}

const WContact* LocalBackend::contact(const string& id) {
	if(id == "me" || id == me.id)
		return &me;
	for(size_t i = 0; i < contactList.size(); i++) {
		if(contactList[i].id == id)
			return &contactList[i];
	}
	return NULL;
}

WSession* LocalBackend::session(const string& talker, bool create) {
	for(size_t i = 0; i < sessionList.size(); i++) {
		if(sessionList[i].talker == talker)
			return &sessionList[i];
	}
	if(!create)
		return NULL;
	WSession s;
	s.talker = talker;
	sessionList.insert(sessionList.begin(), s);
	return &sessionList[0];
}

void LocalBackend::touchSession(WSession* s) {
	/* move to front like WeChat does on new activity; pointers into the
	 * vector are invalidated, callers must re-fetch afterwards */
	for(size_t i = 0; i < sessionList.size(); i++) {
		if(&sessionList[i] == s) {
			if(i == 0)
				return;
			WSession tmp = sessionList[i];
			sessionList.erase(sessionList.begin() + i);
			sessionList.insert(sessionList.begin(), tmp);
			return;
		}
	}
}

string LocalBackend::replyFor(const string& userText) {
	if(userText.find("你好") != string::npos || userText.find("在吗") != string::npos)
		return "你好呀！我在的，有什么事吗？";
	if(userText.find("图片") != string::npos)
		return "图片收到啦，看起来很清晰。";
	if(userText.find("再见") != string::npos || userText.find("拜拜") != string::npos)
		return "好的，回聊！";
	if(userText.find("?") != string::npos || userText.find("？") != string::npos)
		return "这个问题我想想……稍后回复你。";
	return "收到！（本地演示后端自动回复）";
}

void LocalBackend::scheduleReply(const string& talker, const string& userText) {
	PendingReply r;
	r.talker = talker;
	r.text = replyFor(userText);
	r.dueTic = kernel_tic_ms(0) + 2000;
	replies.push_back(r);
}

int64_t LocalBackend::send(const string& talker, const WMessage& msg) {
	WSession* s = session(talker, true);
	if(s == NULL)
		return -1;
	WMessage m = msg;
	m.id = nextMsgId++;
	m.sender = "me";
	m.time = (uint64_t)time(NULL);
	m.pending = false;
	s->messages.push_back(m);
	s->unread = 0;
	touchSession(s);
	if(m.type == "text")
		scheduleReply(talker, m.text);
	return m.id;
}

bool LocalBackend::tick() {
	if(replies.empty())
		return false;
	uint64_t now = kernel_tic_ms(0);
	bool changed = false;
	for(size_t i = 0; i < replies.size();) {
		if(replies[i].dueTic <= now) {
			WSession* s = session(replies[i].talker, true);
			if(s != NULL) {
				WMessage m;
				m.id = nextMsgId++;
				m.type = "text";
				m.text = replies[i].text;
				m.sender = replies[i].talker;
				m.time = (uint64_t)time(NULL);
				s->messages.push_back(m);
				s->unread++;
				touchSession(s);
				changed = true;
			}
			replies.erase(replies.begin() + i);
		}
		else
			i++;
	}
	return changed;
}