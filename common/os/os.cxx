/* Copyright (C) 2010 TightVNC Team.  All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <os/os.h>

#include <assert.h>
#include <stdio.h>

#ifndef WIN32
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#include <wininet.h> /* MinGW needs it */
#include <shlobj.h>
#endif

static int gethomedir(char **dirp, bool userDir)
{
#ifndef WIN32
	char *homedir, *dir;
	size_t len;
	uid_t uid;
	struct passwd *passwd;
#else
	TCHAR *dir;
	BOOL ret;
#endif

	assert(dirp != NULL && *dirp == NULL);

#ifndef WIN32
	homedir = getenv("HOME");
	if (homedir == NULL) {
		uid = getuid();
		passwd = getpwuid(uid);
		if (passwd == NULL) {
			/* Do we want emit error msg here? */
			return -1;
		}
		homedir = passwd->pw_dir;
	}

	len = strlen(homedir);
	dir = new char[len+7];
	if (dir == NULL)
		return -1;

	memcpy(dir, homedir, len);
	if (userDir)
		dir[len]='\0';
	else
		memcpy(dir + len, "/.vnc/\0", 7);
#else
	dir = new TCHAR[MAX_PATH];
	if (dir == NULL)
		return -1;

	if (userDir)
		ret = SHGetSpecialFolderPath(NULL, dir, CSIDL_PROFILE, FALSE);
	else
		ret = SHGetSpecialFolderPath(NULL, dir, CSIDL_APPDATA, FALSE);

	if (ret == FALSE) {
		delete [] dir;
		return -1;
	}
	if (userDir)
		dir[strlen(dir)+1] = '\0';
	else
		memcpy(dir + strlen(dir), (TCHAR *)"\\vnc\\\0", 6);
#endif
	*dirp = dir;
	return 0;
}

int getvnchomedir(char **dirp)
{
	return gethomedir(dirp, false);
}

int getuserhomedir(char **dirp)
{
	return gethomedir(dirp, true);
}

#ifdef WIN32

// GUID with {}
#define GUID_STRING_LENGTH 38
// GUID without {}
#define GUID_STRING_VALUE_LENGTH 36

/* Human-readable string to 128-bit GUID */
bool StringToGUID(const char *szGUID, GUID *g) {
	bool headers = false;

	// Check if string is a valid GUID
	if (!szGUID)
		return false;

	if (szGUID[0] == '{') {
		headers = true;
	}

	if (strlen(szGUID) != (headers? GUID_STRING_LENGTH : GUID_STRING_VALUE_LENGTH))
		return false;

	unsigned int i = 0;
	for (i = 0; i < strlen(szGUID); ++i) {
		char g = szGUID[i];
		
		if (i == 0) {
			if (headers && (g != '{'))
				return false;
		}
		else if (i == (GUID_STRING_VALUE_LENGTH + 1)) {
			if (headers && (g != '}'))
				return false;
		}
		else if ((i == (headers? 9 : 8)) || (i == (headers? 14 : 13)) || (i == (headers? 19 : 18)) || (i == (headers? 24 : 23))) {
			if (g != '-')
				return false;
		}
		else {
			if (!((g >= '0') && (g <= '9')) && !((g >= 'A') && (g <= 'F')) && !((g >= 'a') && (g <= 'f'))) {
				return false;
			}
		}
	}
	
	char *pEnd;
    g->Data1 = strtoul(szGUID + (headers? 1 : 0), &pEnd, 16);
    g->Data2 = strtoul(szGUID + (headers? 10 : 9), &pEnd, 16);
    g->Data3 = strtoul(szGUID + (headers? 15 : 14), &pEnd, 16);

	char b[3];
	b[2] = 0;
	memcpy(&b[0], szGUID + (headers? 20 : 19), 2 * sizeof(b[0]));
	g->Data4[0] = strtoul(&b[0], &pEnd, 16);
	memcpy(&b[0], szGUID + (headers? 22 : 21), 2 * sizeof(b[0]));
	g->Data4[1] = strtoul(&b[0], &pEnd, 16);
	for (i = 0; i < 6; ++i) {
		memcpy(&b[0], szGUID + (headers? 25 : 24) + i * 2, 2 * sizeof(b[0]));
		g->Data4[2 + i] = strtoul(&b[0], &pEnd, 16);
	}

	return true;
}

/* 128-bit GUID to human-readable string */
bool GUIDToString(const GUID *g, char *szGUID, unsigned int iszGUIDLen) {
	if (!g)
		return false;
	
	if (!szGUID)
		return false;

	// Should allow more space the the max length of GUID
  	assert(iszGUIDLen > GUID_STRING_LENGTH);
  	int num = sprintf_s(szGUID, iszGUIDLen, "{%08x-%04x-%04x-%08x-%08x}", g->Data1, g->Data2, g->Data3, *(&(g->Data4[0])), *(&(g->Data4[4])));
  	if (num != GUID_STRING_LENGTH)
    	return false;

  	szGUID[num] = '\0';
  	return true;
}
#endif /* WIN32 */