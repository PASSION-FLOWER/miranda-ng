/*
Copyright � 2016-17 Miranda NG team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

#pragma pack(4)

struct CDiscordCommand
{
	const wchar_t *szCommandId;
	GatewayHandlerFunc pFunc;
}
static handlers[] = // these structures must me sorted alphabetically
{
	{ L"CHANNEL_CREATE", &CDiscordProto::OnChannelCreated },
	{ L"CHANNEL_DELETE", &CDiscordProto::OnChannelDeleted },

	{ L"MESSAGE_ACK",    &CDiscordProto::OnCommandMessageAck },
	{ L"MESSAGE_CREATE", &CDiscordProto::OnCommandMessage },
	{ L"MESSAGE_UPDATE", &CDiscordProto::OnCommandMessage },

	{ L"PRESENCE_UPDATE", &CDiscordProto::OnCommandPresence },

	{ L"READY", &CDiscordProto::OnCommandReady },

	{ L"RELATIONSHIP_ADD", &CDiscordProto::OnCommandFriendAdded },
	{ L"RELATIONSHIP_REMOVE", &CDiscordProto::OnCommandFriendRemoved },

	{ L"TYPING_START", &CDiscordProto::OnCommandTyping },

	{ L"USER_UPDATE", &CDiscordProto::OnCommandUserUpdate },
};

static int __cdecl pSearchFunc(const void *p1, const void *p2)
{
	return wcscmp(((CDiscordCommand*)p1)->szCommandId, ((CDiscordCommand*)p2)->szCommandId);
}

GatewayHandlerFunc CDiscordProto::GetHandler(const wchar_t *pwszCommand)
{
	CDiscordCommand tmp = { pwszCommand, NULL };
	CDiscordCommand *p = (CDiscordCommand*)bsearch(&tmp, handlers, _countof(handlers), sizeof(handlers[0]), pSearchFunc);
	return (p != NULL) ? p->pFunc : NULL;
}

//////////////////////////////////////////////////////////////////////////////////////
// channel operations

void CDiscordProto::OnChannelCreated(const JSONNode &pRoot)
{
	CDiscordUser *pUser = PrepareUser(pRoot["user"]);
	if (pUser != NULL) {
		pUser->channelId = _wtoi64(pRoot["id"].as_mstring());
		setId(pUser->hContact, DB_KEY_CHANNELID, pUser->channelId);
	}
}

void CDiscordProto::OnChannelDeleted(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUserByChannel(pRoot["channel_id"]);
	if (pUser != NULL) {
		pUser->channelId = 0;
		delSetting(pUser->hContact, DB_KEY_CHANNELID);
	}
}

//////////////////////////////////////////////////////////////////////////////////////
// reading a new message

void CDiscordProto::OnCommandFriendAdded(const JSONNode &pRoot)
{
	CDiscordUser *pUser = PrepareUser(pRoot["user"]);
	ProcessType(pUser, pRoot);
}

void CDiscordProto::OnCommandFriendRemoved(const JSONNode &pRoot)
{
	SnowFlake id = _wtoi64(pRoot["id"].as_mstring());
	CDiscordUser *pUser = FindUser(id);
	if (pUser != NULL) {
		if (pUser->hContact) {
			if (pUser->bIsPrivate)
				db_delete_contact(pUser->hContact);
		}
		arUsers.remove(pUser);
	}
}

//////////////////////////////////////////////////////////////////////////////////////
// reading a new message

void CDiscordProto::OnCommandMessage(const JSONNode &pRoot)
{
	PROTORECVEVENT recv = {};
	CMStringW wszMessageId = pRoot["id"].as_mstring();
	SnowFlake messageId = _wtoi64(wszMessageId);
	SnowFlake nonce = _wtoi64(pRoot["nonce"].as_mstring());

	SnowFlake *p = arOwnMessages.find(&nonce);
	if (p != NULL) { // own message? skip it
		debugLogA("skipping own message with nonce=%lld, id=%lld", nonce, messageId);
		return;
	}

	// try to find a sender by his channel
	SnowFlake channelId = _wtoi64(pRoot["channel_id"].as_mstring());
	CDiscordUser *pUser = FindUserByChannel(channelId);
	if (pUser == NULL) {
		debugLogA("skipping message with unknown channel id=%lld", channelId);
		return;
	}

	// if a message has myself as an author, add some flags
	if (_wtoi64(pRoot["author"]["id"].as_mstring()) == m_ownId)
		recv.flags = PREF_CREATEREAD | PREF_SENT;

	CMStringW wszText = pRoot["content"].as_mstring();

	const JSONNode &edited = pRoot["edited_timestamp"];
	if (!edited.isnull())
		wszText.AppendFormat(L" (%s %s)", TranslateT("edited at"), edited.as_mstring().c_str());

	if (pUser->channelId != channelId) {
		debugLogA("failed to process a groupchat message, exiting");
		return;
	}
	
	ptrA buf(mir_utf8encodeW(wszText));
	recv.timestamp = (DWORD)StringToDate(pRoot["timestamp"].as_mstring());
	recv.szMessage = buf;
	recv.lParam = (LPARAM)wszMessageId.c_str();
	ProtoChainRecvMsg(pUser->hContact, &recv);

	SnowFlake lastId = getId(pUser->hContact, DB_KEY_LASTMSGID); // as stored in a database
	if (lastId < messageId)
		setId(pUser->hContact, DB_KEY_LASTMSGID, messageId);
}

//////////////////////////////////////////////////////////////////////////////////////
// someone changed its status

void CDiscordProto::OnCommandMessageAck(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUserByChannel(pRoot["channel_id"]);
	if (pUser != NULL)
		pUser->lastMessageId = _wtoi64(pRoot["message_id"].as_mstring());
}

//////////////////////////////////////////////////////////////////////////////////////
// someone changed its status

void CDiscordProto::OnCommandPresence(const JSONNode &pRoot)
{
	CDiscordUser *pUser = PrepareUser(pRoot["user"]);
	if (pUser == NULL)
		return;

	int iStatus;
	CMStringW wszStatus = pRoot["status"].as_mstring();
	if (wszStatus == L"idle")
		iStatus = ID_STATUS_IDLE;
	else if (wszStatus == L"online")
		iStatus = ID_STATUS_ONLINE;
	else if (wszStatus == L"offline")
		iStatus = ID_STATUS_OFFLINE;
	else
		iStatus = 0;

	if (iStatus != 0)
		setWord(pUser->hContact, "Status", iStatus);

	CMStringW wszGame = pRoot["game"]["name"].as_mstring();
	if (!wszGame.IsEmpty())
		setWString(pUser->hContact, "XStatusMsg", wszGame);
	else
		delSetting(pUser->hContact, "XStatusMsg");		
}

//////////////////////////////////////////////////////////////////////////////////////
// gateway session start

void CALLBACK CDiscordProto::HeartbeatTimerProc(HWND, UINT, UINT_PTR id, DWORD)
{
	((CDiscordProto*)id)->GatewaySendHeartbeat();
}

static void __stdcall sttStartTimer(void *param)
{
	CDiscordProto *ppro = (CDiscordProto*)param;
	SetTimer(g_hwndHeartbeat, (UINT_PTR)param, ppro->getHeartbeatInterval(), &CDiscordProto::HeartbeatTimerProc);
}

void CDiscordProto::OnCommandReady(const JSONNode &pRoot)
{
	GatewaySendHeartbeat();
	CallFunctionAsync(sttStartTimer, this);

	m_szGatewaySessionId = pRoot["session_id"].as_mstring();

	const JSONNode &relations = pRoot["relationships"];
	for (auto it = relations.begin(); it != relations.end(); ++it) {
		const JSONNode &p = *it;

		CDiscordUser *pUser = PrepareUser(p["user"]);
		ProcessType(pUser, p);
	}

	const JSONNode &channels = pRoot["private_channels"];
	for (auto it = channels.begin(); it != channels.end(); ++it) {
		const JSONNode &p = *it;

		CDiscordUser *pUser = NULL;
		const JSONNode &recipients = p["recipients"];
		for (auto it2 = recipients.begin(); it2 != recipients.end(); ++it2)
			pUser = PrepareUser(*it2);

		if (pUser == NULL)
			continue;
			
		pUser->channelId = _wtoi64(p["id"].as_mstring());
		pUser->lastMessageId = _wtoi64(p["last_message_id"].as_mstring());
		pUser->bIsPrivate = true;

		setId(pUser->hContact, DB_KEY_CHANNELID, pUser->channelId);

		SnowFlake oldMsgId = getId(pUser->hContact, DB_KEY_LASTMSGID);
		if (pUser->lastMessageId > oldMsgId)
			RetrieveHistory(pUser->hContact, MSG_AFTER, oldMsgId, 99);
	}
}

//////////////////////////////////////////////////////////////////////////////////////
// UTN support

void CDiscordProto::OnCommandTyping(const JSONNode &pRoot)
{
	SnowFlake userId = _wtoi64(pRoot["user_id"].as_mstring());
	SnowFlake channelId = _wtoi64(pRoot["channel_id"].as_mstring());
	debugLogA("user typing notification: userid=%lld, channelid=%lld", userId, channelId);

	CDiscordUser *pUser = FindUser(userId);
	if (pUser == NULL) {
		debugLogA("user with id=%lld is not found", userId);
		return;
	}

	if (pUser->channelId == channelId) {
		debugLogA("user is typing in his private channel");
		CallService(MS_PROTO_CONTACTISTYPING, pUser->hContact, 20);
	}
	else {
		debugLogA("user is typing in a group channel, skipped");
	}
}

//////////////////////////////////////////////////////////////////////////////////////
// UTN support

void CDiscordProto::OnCommandUserUpdate(const JSONNode &pRoot)
{
	SnowFlake id = _wtoi64(pRoot["id"].as_mstring());

	MCONTACT hContact;
	if (id != m_ownId) {
		CDiscordUser *pUser = FindUser(id);
		if (pUser == NULL)
			return;

		hContact = pUser->hContact;
	}
	else hContact = 0;

	// force rereading avatar
	ptrW wszOldHash(getWStringA(hContact, DB_KEY_AVHASH));
	CMStringW wszNewHash(pRoot["avatar"].as_mstring());
	if (mir_wstrcmp(wszOldHash, wszNewHash)) {
		setWString(hContact, DB_KEY_AVHASH, wszNewHash);
		RetrieveAvatar(hContact);
	}
}
