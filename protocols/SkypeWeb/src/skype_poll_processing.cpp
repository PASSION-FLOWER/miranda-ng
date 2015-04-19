/*
Copyright (c) 2015 Miranda NG project (http://miranda-ng.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "common.h"

void CSkypeProto::ProcessEndpointPresenceRes(JSONNODE *node)
{
	debugLogA("CSkypeProto::ProcessEndpointPresenceRes");
	ptrA selfLink(mir_t2a(ptrT(json_as_string(json_get(node, "selfLink")))));
	ptrA skypename(ContactUrlToName(selfLink));
	if (skypename == NULL)
		return;
	MCONTACT hContact = FindContact(skypename);

	//"publicInfo":{"capabilities":"","typ":"11","skypeNameVersion":"0/7.1.0.105//","nodeInfo":"","version":"24"}
	JSONNODE *publicInfo = json_get(node, "publicInfo");
	if (publicInfo != NULL)
	{
		ptrA skypeNameVersion(mir_t2a(ptrT(json_as_string(json_get(publicInfo, "skypeNameVersion")))));
		ptrA version(mir_t2a(ptrT(json_as_string(json_get(publicInfo, "version")))));
		ptrA typ(mir_t2a(ptrT(json_as_string(json_get(publicInfo, "typ")))));
		if (typ != NULL)
		{
			if (!mir_strcmpi(typ, "17")) //Android
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (Android)");
			else if (!mir_strcmpi(typ, "16")) //iOS
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (iOS)");
			else if (!mir_strcmpi(typ, "12")) //WinRT/Metro
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (WinRT)");
			else if (!mir_strcmpi(typ, "15")) //Winphone
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (WP)");
			else if (!mir_strcmpi(typ, "13")) //OSX
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (OSX)");
			else if (!mir_strcmpi(typ, "11")) //Windows
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (Windows)");
			else if (!mir_strcmpi(typ, "14")) //Linux
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (Linux)");
			else if (!mir_strcmpi(typ, "10"))//XBox ? skypeNameVersion 11/1.8.0.1006
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (XBOX)");
			else if (!mir_strcmpi(typ, "1")) //SkypeWeb
				db_set_s(hContact, m_szModuleName, "MirVer", "Skype (Web)");
			else if (!mir_strcmpi(typ, "125")) //Miranda
			{
				char ver[MAX_PATH];
				mir_snprintf(ver, SIZEOF(ver), "%s %s", skypeNameVersion, version);
				db_set_s(hContact, m_szModuleName, "MirVer", ver);
			}
		}
	}
}

void CSkypeProto::ProcessUserPresenceRes(JSONNODE *node)
{
	debugLogA("CSkypeProto::ProcessUserPresenceRes");

	ptrA selfLink(mir_t2a(ptrT(json_as_string(json_get(node, "selfLink")))));
	ptrA status(mir_t2a(ptrT(json_as_string(json_get(node, "status")))));
	ptrA skypename(ContactUrlToName(selfLink));
	if (skypename == NULL)
	{
		if (IsMe(ptrA(SelfUrlToName(selfLink))))
		{
			int iNewStatus = SkypeToMirandaStatus(status);
			int old_status = m_iStatus;
			m_iDesiredStatus = iNewStatus;
			m_iStatus = iNewStatus;
			if (old_status != iNewStatus)
				ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)old_status, iNewStatus);
			return;
		}
		return;
	}
	MCONTACT hContact = FindContact(skypename);
	SetContactStatus(hContact, SkypeToMirandaStatus(status));
}

void CSkypeProto::ProcessNewMessageRes(JSONNODE *node)
{
	debugLogA("CSkypeProto::ProcessNewMessageRes");

	ptrA conversationLink(mir_t2a(ptrT(json_as_string(json_get(node, "conversationLink")))));

	if (strstr(conversationLink, "/8:"))
		OnPrivateMessageEvent(node);
	else if (strstr(conversationLink, "/19:"))
		OnChatEvent(node);
}

void CSkypeProto::ProcessConversationUpdateRes(JSONNODE *node)
{
	return; //it should be rewritten
}

void CSkypeProto::ProcessThreadUpdateRes(JSONNODE *node)
{
	return;
}