#define PATHSYNC_VER "v0.41BETA"

/*
	PathSync - pathsync.cpp
	Copyright (C) 2004-2007 Cockos Incorporated and others

	Contributors:
	Alan Davies (alan@goatpunch.com)
	Francis Gastellu
	Brennan Underwood

	And now using filename matching from the GNU C library!

	PathSync is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	PathSync is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with PathSync; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
	*/


#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <search.h>
#include <stdlib.h>
#include <time.h>

#include "resource.h"

#include "../WDL/ptrlist.h"
#include "../WDL/wdlstring.h"
#include "../WDL/dirscan.h"
#include "../WDL/fileread.h"
#include "../WDL/filewrite.h"

#include "../WDL/wingui/wndsize.h"
#include "fnmatch.h"

#include "../WDL/win32_utf8.h"

#ifdef _WIN32
#define PREF_DIRSTR "\\"
#else
#define PREF_DIRSTR "/"
#endif


HINSTANCE g_hInstance;

#define ACTION_RECV "Remote->Local"
#define ACTION_SEND "Local->Remote"
#define ACTION_NONE "No Action"
// AD: Descriptive versions, to clarify whether files are
// being created/deleted.
#define ACTION_RECV_CREATE  "Create Local"
#define ACTION_RECV_DELETE  "Delete Local"
#define ACTION_SEND_CREATE  "Create Remote"
#define ACTION_SEND_DELETE  "Delete Remote"
#define REMOTE_ONLY_STR "Remote Only"
#define LOCAL_ONLY_STR "Local Only"

bool action_is_none(const char * str)
{
	return strcmp(str, ACTION_NONE) == 0;
}

bool action_is_recv(const char * str)
{
	return    strcmp(str, ACTION_RECV) == 0
		|| strcmp(str, ACTION_RECV_CREATE) == 0
		|| strcmp(str, ACTION_RECV_DELETE) == 0;
}

bool action_is_send(const char * str)
{
	return    strcmp(str, ACTION_SEND) == 0
		|| strcmp(str, ACTION_SEND_CREATE) == 0
		|| strcmp(str, ACTION_SEND_DELETE) == 0;
}

#define COL_FILENAME  0
#define COL_STATUS  1
#define COL_ACTION  2
#define COL_LOCALSIZE 3
#define COL_REMOTESIZE  4

class dirItem {

public:
	dirItem() { refcnt = 0; }
	~dirItem() { }

	WDL_String relativeFileName;
	WDL_INT64 fileSize;
	FILETIME lastWriteTime;

	int refcnt;
};


char *g_syncactions[] =
{
	"Bidirectional (default)",
	ACTION_SEND " (do not delete missing files/folders)",
	ACTION_RECV " (do not delete missing files/folders)",
	ACTION_SEND,
	ACTION_RECV,
};

WDL_PtrList<WDL_String> m_dirscanlist[2];
WDL_DirScan m_curscanner[2];
WDL_String m_curscanner_relpath[2], m_curscanner_basepath[2];

WDL_PtrList<dirItem> m_files[2];
WDL_PtrList<dirItem> m_listview_recs;

WDL_PtrList<WDL_String> m_include_files;

int g_ignflags, g_defbeh, g_syncfolders; // used only temporarily
int m_comparing; // second and third bits mean done for each side
int m_comparing_pos, m_comparing_pos2;
HWND m_listview;
char m_inifile[2048];
char m_lastsettingsfile[2048];
char g_loadsettingsfile[2048];
bool g_autorun = false;
bool g_systray = false;
bool g_intray = false;
HWND g_copydlg = NULL;
HWND g_dlg = NULL;
int g_lasttraypercent = -1;
int g_throttle, g_throttlespd = 1024;
DWORD g_throttle_sttime;
WDL_INT64 g_throttle_bytes;
int g_numfilesindir;

const int endislist[] = { IDC_STATS, IDC_PATH1, IDC_PATH2, IDC_BROWSE1, IDC_BROWSE2, IDC_IGNORE_SIZE, IDC_IGNORE_DATE, IDC_IGNORE_MISSLOCAL, IDC_IGNORE_MISSREMOTE, IDC_DEFBEHAVIOR, IDC_LOG, IDC_LOGPATH, IDC_LOGBROWSE, IDC_LOCAL_LABEL, IDC_REMOTE_LABEL, IDC_DEFACTIONLABEL, IDC_LOGFILENAMELABEL, IDC_IGNORE_LABEL, IDC_INCLUDE_LABEL, IDC_INCLUDE_FILES, IDC_MASKHELP, IDC_SYNC_FOLDERS };


bool isDirectory(const char * filename)
{
	if (!filename || !filename[0]) return false;
	char c = filename[strlen(filename) - 1];
	return c == '\\' || c == '/';
}

const char *stristr(const char *a, const char *b)
{
	int bl = strlen(b);
	while (*a)
	{
		if (!strnicmp(a, b, bl)) return a;
		a++;
	}
	return NULL;

}

int filenameCompareFunction(dirItem **a, dirItem **b)
{
	const char * pa = (*a)->relativeFileName.Get();
	const char * pb = (*b)->relativeFileName.Get();

	int result = stricmp(pa, pb);

	if (result != 0)
	{
		// AD: Ensure that parent directories sort after the files
		// and subdirectories within them. This avoids problems when
		// deleting parent directories.
		if (isDirectory(pa) && !strnicmp(pb, pa, strlen(pa))) // JF: simplified logic and made better case insensitive
		{
			result = 1;
		}
		else if (isDirectory(pb) && !strnicmp(pa, pb, strlen(pb)))
		{
			result = -1;
		}
	}

	return result;
}

void clearFileLists(HWND hwndDlg)
{
	m_dirscanlist[0].Empty(true);
	m_dirscanlist[1].Empty(true);
	m_files[0].Empty(true);
	m_files[1].Empty(true);

	// dont clear m_listview_recs[], cause they are just references
	ListView_DeleteAllItems(m_listview);
	m_listview_recs.Empty();
}

BOOL WINAPI copyFilesProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI diffToolProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void EnableOrDisableLoggingControls(HWND hwndDlg);

void format_size_string(WDL_INT64 size, char *str)
{
	if (size < 1024) sprintf(str, "%u bytes", (int)size);//BU 'bytes' is more self-explanatory than 'B'
	else if (size < 1048576) sprintf(str, "%.1lf kB", (double)size / 1024.0);//BU kB is more correct
	else if (size < 1073741824) sprintf(str, "%.1lf MB", (double)size / 1048576.0);
	else if (size < 1099511627776i64) sprintf(str, "%.1lf GB", (double)size / 1073741824.0);
	else sprintf(str, "%.1lf TB", (double)size / 1099511627776.0);
}

FILE * g_log = 0;

void RestartLogging(HWND hwndDlg)
{
	// Always close and re-open, filename may have changed,
	// logging may have been disabled in UI.
	if (g_log)
	{
		fclose(g_log);
		g_log = 0;
	}

	if (IsDlgButtonChecked(hwndDlg, IDC_LOG))
	{
		char name[1024] = "";
		GetDlgItemText(hwndDlg, IDC_LOGPATH, name, sizeof name);

		// AD: Don't try to open an empty filename
		if (strlen(name))
		{
			g_log = fopen(name, "at");
		}

		if (!g_log)
		{
			char message[2048];
			sprintf(message, "Couldn't open logfile %s", name);
			MessageBox(hwndDlg, message, "Error", 0);
		}
	}
}

void LogMessage(const char * pStr)
{
	if (g_log)
	{
		char timestr[100] = "";
		time_t curtime = time(0);
		strftime(timestr, sizeof timestr - 1, "%Y-%m-%d %H:%M:%S ", localtime(&curtime));
		fprintf(g_log, timestr);
		fprintf(g_log, pStr);
		fprintf(g_log, "\n");
		fflush(g_log);
	}
}

WDL_INT64 m_total_copy_size = 0;

void calcStats(HWND hwndDlg)
{
	WDL_INT64 totalbytescopy = 0;
	int totalfilesdelete = 0, totalfilescopy = 0;
	int x, l = ListView_GetItemCount(m_listview);
	for (x = 0; x < l; x++)
	{
		char action[128];
		LVITEM lvi = { LVIF_PARAM | LVIF_TEXT, x, 2 };
		lvi.pszText = action;
		lvi.cchTextMax = sizeof(action);
		ListView_GetItem(m_listview, &lvi);

		int x = lvi.lParam;
		dirItem **its = m_listview_recs.GetList() + x;
		// AD: Use wrapper functions to compare actions
		if (!action_is_none(action))
		{
			int isSend = action_is_send(action);

			if (its[!isSend])
			{
				totalfilescopy++;
				totalbytescopy += its[!isSend]->fileSize;
			}
			else // delete
			{
				totalfilesdelete++;
			}
			// calculate loc/rem here
		}
	}
	char buf[1024];
	strcpy(buf, "Synchronizing will ");
	if (totalfilescopy)
	{
		char tmp[128];
		format_size_string(totalbytescopy, tmp);
		sprintf(buf + strlen(buf), "copy %s in %d file%s", tmp, totalfilescopy, totalfilescopy == 1 ? "" : "s");
	}
	if (totalfilesdelete)
	{
		if (totalfilescopy) strcat(buf, ", and ");
		sprintf(buf + strlen(buf), "delete %d file%s", totalfilesdelete, totalfilesdelete == 1 ? "" : "s");
	}

	if (!totalfilesdelete && !totalfilescopy)
	{
		strcat(buf, "not perform any actions");
		EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 0);
	}
	else EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 1);

	strcat(buf, ".");//BU added
	LogMessage(buf);

	strcat(buf, " (Right click on items to change their actions.)");//BU
	SetDlgItemText(hwndDlg, IDC_STATS, buf);
	m_total_copy_size = totalbytescopy;
}

void set_current_settings_file(HWND hwndDlg, char *fn)
{
	lstrcpyn(m_lastsettingsfile, fn, sizeof(m_lastsettingsfile));

	char buf[4096];
	char *p = fn;
	while (*p) p++;
	while (p >= fn && *p != '\\' && *p != '/') p--;
	sprintf(buf, "PathSync " PATHSYNC_VER " - Analysis - %s", p + 1);
	SetWindowText(hwndDlg, buf);
}

#define WM_SYSTRAY              WM_USER + 0x200
#define WM_COPYDIALOGEND        WM_USER + 0x201
#define CMD_EXITPATHSYNC        30000
#define CMD_SHOWWINDOWFROMTRAY  30001
#define CMD_CANCELCOPY          30002
#define CMD_CANCELANALYSIS      30003

BOOL systray_add(HWND hwnd, UINT uID, HICON hIcon, LPSTR lpszTip)
{
	NOTIFYICONDATA tnid;
	tnid.cbSize = sizeof(NOTIFYICONDATA);
	tnid.hWnd = hwnd;
	tnid.uID = uID;
	tnid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	tnid.uCallbackMessage = WM_SYSTRAY;
	tnid.hIcon = hIcon;
	lstrcpyn(tnid.szTip, lpszTip, sizeof(tnid.szTip) - 1);
	return (Shell_NotifyIcon(NIM_ADD, &tnid));
}

BOOL systray_del(HWND hwnd, UINT uID) {
	NOTIFYICONDATA tnid;
	tnid.cbSize = sizeof(NOTIFYICONDATA);
	tnid.hWnd = hwnd;
	tnid.uID = uID;
	return(Shell_NotifyIcon(NIM_DELETE, &tnid));
}

BOOL systray_mod(HWND hwnd, UINT uID, LPSTR lpszTip) {
	NOTIFYICONDATA tnid;
	tnid.cbSize = sizeof(NOTIFYICONDATA);
	tnid.hWnd = hwnd;
	tnid.uID = uID;
	tnid.uFlags = NIF_TIP;
	strncpy(tnid.szTip, lpszTip, sizeof(tnid.szTip) - 1);
	return (Shell_NotifyIcon(NIM_MODIFY, &tnid));
}

void show_window_from_tray()
{
	HWND wnd;
	if (g_copydlg) wnd = g_copydlg;
	else wnd = g_dlg;
	if (IsIconic(wnd)) ShowWindow(wnd, SW_RESTORE);
	else ShowWindow(wnd, SW_NORMAL);
	SetForegroundWindow(wnd);
	g_intray = false;
}

void free_pattern_list(WDL_PtrList<WDL_String> *list)
{
	while (list->GetSize() > 0)
	{
		int idx = list->GetSize() - 1;
		delete list->Get(idx);
		list->Delete(idx);
	}
}

void parse_pattern_list(HWND hwndDlg, char *str, WDL_PtrList<WDL_String> *list)
{
	char pattern[2048] = "";
	char *p = str;
	char *d = pattern;
	while (*p)
	{
		if (*p == ';')
		{
			*d = 0;
			list->Add(new WDL_String(pattern));
			*pattern = 0;
			d = pattern,
				p++;
			continue;
		}
		*d++ = *p++;
	}
	if (*pattern)
	{
		*d = 0;
		list->Add(new WDL_String(pattern));
	}
}

int test_file_pattern(char *file, int is_dir)
{
	int s = m_include_files.GetSize();
	if (!s) return 1;

	for (int i = 0; i < s; i++)
	{
		char *p = m_include_files.Get(i)->Get();
		int isnot = 0;
		if (*p == '!') isnot++, p++;

		if (is_dir) // we do not want to exclude anything by this list, rather just 
			// detect things that might be valid. 
		{
			if (*p == '*' && !isnot) return 1; // detect *.bla wildcards

			int l = strlen(file);
			while (l>0 && (file[l - 1] == '\\' || file[l - 1] == '/')) l--;
			if (!strnicmp(p, file, l))
			{
				if (!p[l] || p[l] == '\\' || p[l] == '/')
				{
					if (!isnot) return 1;

					// not, so make sure it ends either in 0 \ or \*
					if (!p[l] ||
						!p[l + 1] ||
						!strcmp(p + l + 1, "*"))
						return 0;
				}
			}
		}

		if (fnmatch(p, file, 0) == 0) return !isnot;
	}
	return 0;
}

void enableAllButtonsInList(HWND hwndDlg)
{
	int x;
	for (x = 0; x < sizeof(endislist) / sizeof(endislist[0]); x++)
		EnableWindow(GetDlgItem(hwndDlg, endislist[x]), 1);
	// AD: ensure logging filename and browse button are correctly enabled
	EnableOrDisableLoggingControls(hwndDlg);
}

void stopAnalyzeAndClearList(HWND hwndDlg)
{
	if (m_comparing)
	{
		KillTimer(hwndDlg, 32);
		SetDlgItemText(hwndDlg, IDC_ANALYZE, "Analyze!");
		SetDlgItemText(hwndDlg, IDC_STATUS, "Status: Stopped");
		m_comparing = 0;
		enableAllButtonsInList(hwndDlg);
		free_pattern_list(&m_include_files);
		systray_mod(hwndDlg, 0, "PathSync");
		g_lasttraypercent = -1;
	}
	clearFileLists(hwndDlg);
	SetDlgItemText(hwndDlg, IDC_STATS, "");
	SetDlgItemText(hwndDlg, IDC_STATUS, "");
	EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 0);
}

int load_settings(HWND hwndDlg, char *sec, char *fn) // return version
{
	char path[2048];
	GetPrivateProfileString(sec, "path1", "", path, sizeof(path), fn);
	SetDlgItemText(hwndDlg, IDC_PATH1, path);
	GetPrivateProfileString(sec, "path2", "", path, sizeof(path), fn);
	SetDlgItemText(hwndDlg, IDC_PATH2, path);
	int ignflags = GetPrivateProfileInt(sec, "ignflags", 0, fn);
	CheckDlgButton(hwndDlg, IDC_IGNORE_SIZE, (ignflags & 1) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_IGNORE_DATE, (ignflags & 16) ? BST_INDETERMINATE : ((ignflags & 2) ? BST_CHECKED : BST_UNCHECKED));
	CheckDlgButton(hwndDlg, IDC_IGNORE_MISSLOCAL, (ignflags & 4) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, IDC_IGNORE_MISSREMOTE, (ignflags & 8) ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessage(hwndDlg, IDC_DEFBEHAVIOR, CB_SETCURSEL, (WPARAM)GetPrivateProfileInt(sec, "defbeh", 0, fn), 0);
	GetPrivateProfileString(sec, "logpath", "", path, sizeof(path), fn);
	int syncfolders = GetPrivateProfileInt(sec, "syncfolders", 1, fn);
	CheckDlgButton(hwndDlg, IDC_SYNC_FOLDERS, syncfolders ? BST_CHECKED : BST_UNCHECKED);

	if (strlen(path) && path[0] != '!')
	{
		CheckDlgButton(hwndDlg, IDC_LOG, 1);
	}
	else
	{
		CheckDlgButton(hwndDlg, IDC_LOG, 0);
	}
	SetDlgItemText(hwndDlg, IDC_LOGPATH, path[0] == '!' ? path + 1 : path);

	GetPrivateProfileString(sec, "include", "", path, sizeof(path), fn);
	SetDlgItemText(hwndDlg, IDC_INCLUDE_FILES, path);

	g_throttle = GetPrivateProfileInt(sec, "throttle", 0, fn);
	g_throttlespd = GetPrivateProfileInt(sec, "throttlespd", 1024, fn);



	return GetPrivateProfileInt(sec, "pssversion", 0, fn);
}


void save_settings(HWND hwndDlg, char *sec, char *fn)
{
	char path[2048];
	if (strcmp(sec, "config")) WritePrivateProfileString(sec, "pssversion", "1", fn);

	GetDlgItemText(hwndDlg, IDC_PATH1, path, sizeof(path));
	WritePrivateProfileString(sec, "path1", path, fn);
	GetDlgItemText(hwndDlg, IDC_PATH2, path, sizeof(path));
	WritePrivateProfileString(sec, "path2", path, fn);
	int ignflags = 0;
	if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_SIZE)) ignflags |= 1;
	if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_DATE))
	{
		if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_DATE) == BST_INDETERMINATE) ignflags |= 16;
		else ignflags |= 2;
	}

	if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_MISSLOCAL)) ignflags |= 4;
	if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_MISSREMOTE)) ignflags |= 8;
	wsprintf(path, "%d", ignflags);
	WritePrivateProfileString(sec, "ignflags", path, fn);
	wsprintf(path, "%d", SendDlgItemMessage(hwndDlg, IDC_DEFBEHAVIOR, CB_GETCURSEL, 0, 0));
	WritePrivateProfileString(sec, "defbeh", path, fn);

	if (IsWindowEnabled(GetDlgItem(hwndDlg, IDC_LOG)))
	{
		GetDlgItemText(hwndDlg, IDC_LOGPATH, path, sizeof(path));
		WritePrivateProfileString(sec, "logpath", path, fn);
	}
	else
	{
		path[0] = '!';
		GetDlgItemText(hwndDlg, IDC_LOGPATH, path + 1, sizeof(path) - 1);
		WritePrivateProfileString(sec, "logpath", path, fn);
	}
	GetDlgItemText(hwndDlg, IDC_INCLUDE_FILES, path, sizeof(path));
	WritePrivateProfileString(sec, "include", path, fn);

	wsprintf(path, "%d", g_throttlespd);
	WritePrivateProfileString(sec, "throttlespd", path, fn);
	WritePrivateProfileString(sec, "throttle", g_throttle ? "1" : "0", fn);

	int syncfolders = IsDlgButtonChecked(hwndDlg, IDC_SYNC_FOLDERS);
	wsprintf(path, "%d", syncfolders);
	::WritePrivateProfileString(sec, "syncfolders", path, fn);
}

int CALLBACK MyBrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED)
	{
		SendMessage(hWnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}

void EnableOrDisableLoggingControls(HWND hwndDlg)
{
	if (IsDlgButtonChecked(hwndDlg, IDC_LOG))
	{
		EnableWindow(GetDlgItem(hwndDlg, IDC_LOGPATH), 1);
		EnableWindow(GetDlgItem(hwndDlg, IDC_LOGBROWSE), 1);
	}
	else
	{
		EnableWindow(GetDlgItem(hwndDlg, IDC_LOGPATH), 0);
		EnableWindow(GetDlgItem(hwndDlg, IDC_LOGBROWSE), 0);
	}
}

void cancel_analysis(HWND dlg)
{
	if (m_comparing)
	{
		KillTimer(dlg, 32);
		SetDlgItemText(dlg, IDC_ANALYZE, "Analyze!");
		SetDlgItemText(dlg, IDC_STATUS, "Status: Stopped");
		m_comparing = 0;
		enableAllButtonsInList(dlg);
		free_pattern_list(&m_include_files);
		systray_mod(dlg, 0, "PathSync");
		g_lasttraypercent = -1;
	}
	if (g_autorun) PostQuitMessage(1);
}

BOOL WINAPI mainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static WDL_WndSizer resizer;
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		resizer.init(hwndDlg);
		resizer.init_item(IDC_PATH1, 0, 0, 0.5, 0);
		resizer.init_item(IDC_BROWSE1, 0.5, 0, 0.5, 0);
		resizer.init_item(IDC_REMOTE_LABEL, 0.5, 0, 0.5, 0);
		resizer.init_item(IDC_PATH2, 0.5, 0, 1.0, 0);
		resizer.init_item(IDC_LOGPATH, 0.0, 0, 1.0, 0);//BU added
		resizer.init_item(IDC_BROWSE2, 1.0, 0, 1.0, 0);
		resizer.init_item(IDC_ANALYZE, 1.0, 0, 1.0, 0);
		resizer.init_item(IDC_SYNCHROPATHS, 0.0, 0, 1.0, 0);//BU added
		resizer.init_item(IDC_LOGGING, 0.0, 0, 1.0, 0);//BU added
		resizer.init_item(IDC_STATUS, 0, 0, 1.0, 0);
		resizer.init_item(IDC_LIST1, 0, 0, 1.0, 1.0);
		resizer.init_item(IDC_STATS, 0, 1.0, 1.0, 1.0);
		resizer.init_item(IDC_GO, 1.0, 1.0, 1.0, 1.0);
		resizer.init_item(IDC_DEFBEHAVIOR, 0.0, 0.0, 1.0, 0.0);
		resizer.init_item(IDC_INCLUDE_FILES, 0.0, 0, 1, 0);
		resizer.init_item(IDC_MASKHELP, 1, 0, 1, 0);
		resizer.init_item(IDC_LOGBROWSE, 1, 0, 1, 0);//BU added
		resizer.init_item(IDC_SYNC_FOLDERS, 1.0, 0, 1.0, 0);// AD added

		SetWindowText(hwndDlg, "PathSync " PATHSYNC_VER " - Analysis");

		HICON icon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));
		SetClassLong(hwndDlg, GCL_HICON, (long)icon);
		if (g_systray) systray_add(hwndDlg, 0, icon, "PathSync");
		m_listview = GetDlgItem(hwndDlg, IDC_LIST1);

		WDL_UTF8_HookListView(m_listview);

#ifdef _WIN32
		ListView_SetExtendedListViewStyleEx(m_listview, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
#endif

		{
			LVCOLUMN lvc = { LVCF_TEXT | LVCF_WIDTH, 0, 270, "Filename" };
			ListView_InsertColumn(m_listview, COL_FILENAME, &lvc);
			lvc.pszText = "Status";
			lvc.cx = 140;
			ListView_InsertColumn(m_listview, COL_STATUS, &lvc);
			lvc.pszText = "Action";
			lvc.cx = 90;
			ListView_InsertColumn(m_listview, COL_ACTION, &lvc);
			lvc.pszText = "Local Size";
			lvc.cx = 70;
			ListView_InsertColumn(m_listview, COL_LOCALSIZE, &lvc);
			lvc.pszText = "Remote Size";
			lvc.cx = 75;
			ListView_InsertColumn(m_listview, COL_REMOTESIZE, &lvc);

			int x;
			for (x = 0; x < sizeof(g_syncactions) / sizeof(g_syncactions[0]); x++)
			{
				SendDlgItemMessage(hwndDlg, IDC_DEFBEHAVIOR, CB_ADDSTRING, 0, (LPARAM)g_syncactions[x]);
			}

		}

		SetDlgItemText(hwndDlg, IDC_STATS, "");

		if (g_loadsettingsfile[0] && load_settings(hwndDlg, "pathsync settings", g_loadsettingsfile) > 0)
		{
			set_current_settings_file(hwndDlg, g_loadsettingsfile);
		}
		else
		{
			if (g_loadsettingsfile[0])
			{
				if (g_autorun)
				{
					PostQuitMessage(1);
				}
				else
					MessageBox(hwndDlg, "Error loading PSS file, loading last config", "PathSync Warning", MB_OK);
			}
			load_settings(hwndDlg, "config", m_inifile);
		}
		g_loadsettingsfile[0] = 0;

		EnableOrDisableLoggingControls(hwndDlg);

		if (g_autorun)
		{
			if (IsWindowEnabled(GetDlgItem(hwndDlg, IDC_ANALYZE)))
			{
				PostMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ANALYZE, 1), (LPARAM)GetDlgItem(hwndDlg, IDC_ANALYZE));
			}
			else if (g_autorun)
			{
				PostQuitMessage(1);
			}
		}
	}

	return 0;
	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO p = (LPMINMAXINFO)lParam;
		p->ptMinTrackSize.x = 630;//BU expanded these
		p->ptMinTrackSize.y = 470;//BU expanded these
	}
	return 0;
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED) {
			resizer.onResize();
		}
		if (g_systray)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				g_intray = true;
				ShowWindow(hwndDlg, SW_HIDE);
			}
			else if (wParam == SIZE_RESTORED)
			{
				g_intray = false;
				ShowWindow(hwndDlg, SW_SHOW);
			}
		}
		return 0;

	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_CLOSE: PostQuitMessage(0); break;
		}
		return 0;
	case WM_DESTROY:
		systray_del(hwndDlg, 0);
		save_settings(hwndDlg, "config", m_inifile);
		return 0;
	case WM_SYSTRAY:
		switch (LOWORD(lParam))
		{
		case WM_LBUTTONDOWN:
			show_window_from_tray();
			return 0;
		case WM_RBUTTONUP:
		{
			SetForegroundWindow(g_dlg);
			HMENU popup = CreatePopupMenu();
			POINT pt;
			GetCursorPos(&pt);
			if (g_intray)
			{
				InsertMenu(popup, -1, MF_STRING | MF_BYCOMMAND, CMD_SHOWWINDOWFROMTRAY, g_copydlg ? "&Show Progress" : "&Show Analysis");
				if (m_comparing) InsertMenu(popup, -1, MF_STRING | MF_BYCOMMAND, CMD_CANCELANALYSIS, "&Cancel Analysis");
				if (g_copydlg) InsertMenu(popup, -1, MF_STRING | MF_BYCOMMAND, CMD_CANCELCOPY, "&Cancel Synchronization");
				if (!g_copydlg && !m_comparing) InsertMenu(popup, -1, MF_SEPARATOR, 0, NULL);
			}
			if (!g_copydlg && !m_comparing) InsertMenu(popup, -1, MF_STRING | MF_BYCOMMAND, CMD_EXITPATHSYNC, "&Exit PathSync");
			SetMenuDefaultItem(popup, g_intray ? CMD_SHOWWINDOWFROMTRAY : CMD_EXITPATHSYNC, FALSE);
			TrackPopupMenuEx(popup, TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, g_dlg, NULL);
		}
		return 0;
		}
		return 0;
	case WM_DROPFILES:
	{
		HDROP hDrop = (HDROP)wParam;
		char buf[2048];
		if (DragQueryFile(hDrop, 0, buf, sizeof(buf)) > 4)
		{
			if (!stricmp(buf + strlen(buf) - 4, ".pss"))
			{
				stopAnalyzeAndClearList(hwndDlg);
				if (load_settings(hwndDlg, "pathsync settings", buf) > 0)
				{
					set_current_settings_file(hwndDlg, buf);
				}
			}
		}
		DragFinish(hDrop);
	}
	break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			// todo: shell integration
		case IDM_LOAD_SYNC_SETTINGS:
		{
			char cpath[MAX_PATH * 2];
			char temp[4096];
			OPENFILENAME l = { sizeof(l), };
			strcpy(temp, m_lastsettingsfile);
			l.hwndOwner = hwndDlg;
			l.lpstrFilter = "PathSync Settings (*.PSS)\0*.PSS\0All Files\0*.*\0\0";
			l.lpstrFile = temp;
			l.nMaxFile = sizeof(temp) - 1;
			l.lpstrTitle = "Load SyncSettings from file:";
			l.lpstrDefExt = "PSS";
			GetCurrentDirectory(MAX_PATH * 2, cpath);
			l.lpstrInitialDir = cpath;
			l.Flags = OFN_HIDEREADONLY | OFN_EXPLORER;
			if (GetOpenFileName(&l))
			{
				stopAnalyzeAndClearList(hwndDlg);
				if (load_settings(hwndDlg, "pathsync settings", temp) > 0)
					set_current_settings_file(hwndDlg, temp);
			}
		}
		break;
		case IDM_SAVE_SYNC_SETTINGS:
		{
			char cpath[MAX_PATH * 2];
			char temp[4096];
			strcpy(temp, m_lastsettingsfile);
			OPENFILENAME l = { sizeof(l), };
			l.hwndOwner = hwndDlg;
			l.lpstrFilter = "PathSync Settings (*.PSS)\0*.PSS\0All Files\0*.*\0\0";
			l.lpstrFile = temp;
			l.nMaxFile = sizeof(temp) - 1;
			l.lpstrTitle = "Save SyncSettings to file:";
			l.lpstrDefExt = "PSS";
			GetCurrentDirectory(MAX_PATH * 2, cpath);
			l.lpstrInitialDir = cpath;
			l.Flags = OFN_HIDEREADONLY | OFN_EXPLORER;
			if (GetSaveFileName(&l))
			{
				save_settings(hwndDlg, "pathsync settings", temp);
				set_current_settings_file(hwndDlg, temp);
			}
		}
		break;
		case IDM_ABOUT:
			MessageBox(hwndDlg, "PathSync " PATHSYNC_VER "\r\nCopyright (C) 2004-2007, Cockos Incorporated and others\r\n"
				"For updates visit http://www.cockos.com/pathsync/\r\n"
				"\r\n"
				"PathSync is free software; you can redistribute it and/or modify\r\n"
				"it under the terms of the GNU General Public License as published by\r\n"
				"the Free Software Foundation; either version 2 of the License, or\r\n"
				"(at your option) any later version.\r\n"
				"\r\n"
				"PathSync is distributed in the hope that it will be useful,\r\n"
				"but WITHOUT ANY WARRANTY; without even the implied warranty of\r\n"
				"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\r\n"
				"GNU General Public License for more details.\r\n"
				"\r\n"
				"You should have received a copy of the GNU General Public License\r\n"
				"along with PathSync; if not, write to the Free Software\r\n"
				"Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\r\n"
				,

				"About PathSync", MB_OK);
			break;
		case IDM_EXIT:
			PostQuitMessage(0);
			break;
		case CMD_SHOWWINDOWFROMTRAY:
			show_window_from_tray();
			break;
		case CMD_EXITPATHSYNC:
			if (!g_copydlg) PostQuitMessage(0);
			break;
		case CMD_CANCELCOPY:
			if (g_copydlg) PostMessage(g_copydlg, WM_COMMAND, IDCANCEL, 0);
			break;
		case CMD_CANCELANALYSIS:
			cancel_analysis(hwndDlg);
			break;
		case IDC_DEFBEHAVIOR:
			if (HIWORD(wParam) == CBN_SELCHANGE)
			{
				stopAnalyzeAndClearList(hwndDlg);
			}
			break;
		case IDC_SYNC_FOLDERS:
			stopAnalyzeAndClearList(hwndDlg);
			break;
		case IDC_ANALYZE:
			if (m_comparing)
			{
				cancel_analysis(hwndDlg);
			}
			else
			{
				SetDlgItemText(hwndDlg, IDC_ANALYZE, "Stop...");//BU change button text immediately (ie if a HD is spinning up or something)
				RestartLogging(hwndDlg);
				LogMessage("Analysis");
				systray_mod(hwndDlg, 0, "PathSync - Analysis in progress");

				g_ignflags = 0;
				if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_SIZE)) g_ignflags |= 1;
				if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_DATE))
				{
					if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_DATE) == BST_INDETERMINATE) g_ignflags |= 16;
					else g_ignflags |= 2;
				}
				if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_MISSLOCAL)) g_ignflags |= 4;
				if (IsDlgButtonChecked(hwndDlg, IDC_IGNORE_MISSREMOTE)) g_ignflags |= 8;
				g_defbeh = SendDlgItemMessage(hwndDlg, IDC_DEFBEHAVIOR, CB_GETCURSEL, 0, 0);
				g_syncfolders = IsDlgButtonChecked(hwndDlg, IDC_SYNC_FOLDERS);

				clearFileLists(hwndDlg);
				SetDlgItemText(hwndDlg, IDC_STATS, "");

				char buf[2048];
				GetDlgItemText(hwndDlg, IDC_PATH1, buf, sizeof(buf));
				while (buf[0] && (buf[strlen(buf) - 1] == '\\' || buf[strlen(buf) - 1] == '/')) buf[strlen(buf) - 1] = 0;
				m_curscanner_basepath[0].Set(buf);
				GetDlgItemText(hwndDlg, IDC_PATH2, buf, sizeof(buf));
				while (buf[0] && (buf[strlen(buf) - 1] == '\\' || buf[strlen(buf) - 1] == '/')) buf[strlen(buf) - 1] = 0;
				m_curscanner_basepath[1].Set(buf);

				// just in case it didn't get cleared at the end of the last analysis, somehow
				free_pattern_list(&m_include_files);

				GetDlgItemText(hwndDlg, IDC_INCLUDE_FILES, buf, sizeof(buf));
				parse_pattern_list(hwndDlg, buf, &m_include_files);

				m_curscanner_relpath[0].Set("");
				m_curscanner_relpath[1].Set("");

				WDL_String msgLocal("Local Path: ");
				msgLocal.Append(m_curscanner_basepath[0].Get());
				LogMessage(msgLocal.Get());

				WDL_String msgRemote("Remote Path: ");
				msgRemote.Append(m_curscanner_basepath[1].Get());
				LogMessage(msgRemote.Get());

				if (m_curscanner[0].First(m_curscanner_basepath[0].Get()))
				{
					WDL_String msg("Error reading path: ");
					msg.Append(m_curscanner_basepath[0].Get());
					if (!g_autorun)
					{
						MessageBox(hwndDlg, msg.Get(), "Error", MB_OK);
					}
					LogMessage(msg.Get());
					// AD: fixed bug, button text would stay as 'Stop...'.
					SetDlgItemText(hwndDlg, IDC_ANALYZE, "Analyze!");
				}
				else if (m_curscanner[1].First(m_curscanner_basepath[1].Get()))
				{
					WDL_String msg("Error reading path: ");
					msg.Append(m_curscanner_basepath[1].Get());
					if (!g_autorun)
					{
						MessageBox(hwndDlg, msg.Get(), "Error", MB_OK);
					}
					LogMessage(msg.Get());
					// AD: fixed bug, button text would stay as 'Stop...'.
					SetDlgItemText(hwndDlg, IDC_ANALYZE, "Analyze!");
				}
				else
				{
					// start new scan
					m_comparing = 1;
					SetTimer(hwndDlg, 32, 50, NULL);
					int x;
					for (x = 0; x < sizeof(endislist) / sizeof(endislist[0]); x++)
						EnableWindow(GetDlgItem(hwndDlg, endislist[x]), 0);
					EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 0);
				}

			}
			break;
		case IDOK:
			break;
		case IDC_MASKHELP:
			MessageBox(hwndDlg, "The filename mask box allows you to specify a list of rules to\r\n"
				"let you either include or exclude files from the analysis/copy.\r\n"
				"The rules are seperated by semicolons, and are evaluated from left\r\n"
				"to right. When a rule is matched, no further rules are checked.\r\n"
				"Rules beginning with !, when matched, mean the item is excluded.\r\n"
				"Examples:\r\n"
				"\t*\t\t (includes everything)\r\n"
				"\t!*.pch;*\t\t (includes everything but files ending in .pch)\r\n"
				"\t*.mp3;*.jpg;*.avi\t(includes only mp3, jpg, and avi files)\r\n"
				"\t!temp\\;*\t\t(includes everything except the temp\\ directory)\r\n"

				, "Filename Mask Help", MB_OK);
			break;
		case IDC_BROWSE1:
		case IDC_BROWSE2:
		{
			char name[1024];
			char oldpath[MAX_PATH];

			// Modified 10/24/2006 J. Puhlmann
			// Add automatic selection of previous selected path in directory browser window
			// BROWSEINFO bi={hwndDlg,NULL,name,"Choose a Directory",BIF_RETURNONLYFSDIRS,NULL,};
			GetDlgItemText(hwndDlg, LOWORD(wParam) == IDC_BROWSE1 ? IDC_PATH1 : IDC_PATH2, oldpath, sizeof(oldpath));
			BROWSEINFO bi = { hwndDlg, NULL, name, "Choose a Directory", BIF_RETURNONLYFSDIRS, MyBrowseCallbackProc, (LPARAM)oldpath };

			ITEMIDLIST *id = SHBrowseForFolder(&bi);
			if (id)
			{
				SHGetPathFromIDList(id, name);

				IMalloc *m;
				SHGetMalloc(&m);
				m->Free(id);
				SetDlgItemText(hwndDlg, LOWORD(wParam) == IDC_BROWSE1 ? IDC_PATH1 : IDC_PATH2, name);
				m->Release();
			}
		}
		break;

		case IDC_DRIVER:{
			//change local driver
			char path[1024] = "";
			GetDlgItemText(hwndDlg, IDC_PATH1, path, sizeof path);
			char localDriver = path[0] = m_inifile[0];
			SetDlgItemText(hwndDlg, IDC_PATH1, path);

			GetDlgItemText(hwndDlg, IDC_PATH2, path, sizeof path);
			path[0] = 67;// from C driver
			//WDL_String ret = "";
			DWORD ret;
			for (size_t i = 0; i < 24; i++)
			{
				
				if (path[0] != localDriver){
					ret = GetFileAttributes(path);
					if (ret==22||ret==16)
					{
						SetDlgItemText(hwndDlg, IDC_PATH2, path);
						break;
					}
				}
				path[0]++;
			}
		}
						break;

		case IDC_LOGBROWSE:
		{
			char name[1024] = "";
			OPENFILENAME ofn = { 0 };
			ofn.lStructSize = sizeof OPENFILENAME;
			ofn.hwndOwner = hwndDlg;
			ofn.lpstrTitle = "Choose a Log File";
			ofn.lpstrFilter = "Log Files (.log)\0*.log\0All Files (*.*)\0*.*\0";
			ofn.lpstrFile = name;
			ofn.nMaxFile = sizeof name;

			if (GetOpenFileName(&ofn))
			{
				SetDlgItemText(hwndDlg, IDC_LOGPATH, name);
			}
		}
		break;
		case IDC_GO:
			if (ListView_GetItemCount(m_listview))
			{
				if (!g_autorun) // no need for duplicate request to run during autorun
				{
					RestartLogging(hwndDlg);
				}
				LogMessage("Sync");

				ShowWindow(hwndDlg, SW_HIDE);
				g_copydlg = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG2), NULL, copyFilesProc);
				if (!g_systray || !g_intray) ShowWindow(g_copydlg, SW_NORMAL);
			}
			break;
		case IDC_LOG:
			EnableOrDisableLoggingControls(hwndDlg);
			break;
		case IDC_SETCOMPARE:
			EnableWindow(g_dlg, FALSE);
			DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_DIFFTOOL), NULL, diffToolProc);
			EnableWindow(g_dlg, TRUE);
			SetFocus(g_dlg);
			break;
		}
		break;
	case WM_COPYDIALOGEND:
		DestroyWindow(g_copydlg);
		g_copydlg = NULL;
		clearFileLists(hwndDlg);
		SetDlgItemText(hwndDlg, IDC_STATS, "");
		SetDlgItemText(hwndDlg, IDC_STATUS, "");
		EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 0);
		if (!g_systray || !g_intray) ShowWindow(hwndDlg, SW_SHOW);

		if (g_autorun)
		{
			PostQuitMessage(1);
		}
		return 0;
	case WM_NOTIFY:
	{
		LPNMHDR l = (LPNMHDR)lParam;
		if (l->idFrom == IDC_LIST1 && l->code == NM_RCLICK && !m_comparing && ListView_GetSelectedCount(m_listview))
		{
			HMENU h = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_MENU1));
			if (h)
			{
				HMENU h2 = GetSubMenu(h, 0);
				if (h2)
				{
					int sel = 0;
					int localonly = 0;
					int remoteonly = 0;

					POINT p;
					GetCursorPos(&p);

					int y, selcount = 0, count = ListView_GetItemCount(m_listview);
					for (y = 0; y < count; y++)
					{
						if (ListView_GetItemState(m_listview, y, LVIS_SELECTED) & LVIS_SELECTED)
						{
							selcount++;
							char buf[128];
							ListView_GetItemText(m_listview, y, COL_STATUS, buf, sizeof(buf));

							if (!strcmp(buf, LOCAL_ONLY_STR)) localonly++;
							else if (!strcmp(buf, REMOTE_ONLY_STR)) remoteonly++;


							ListView_GetItemText(m_listview, y, COL_ACTION, buf, sizeof(buf));
							// AD: Use wrappers to compare
							if (action_is_recv(buf)) sel |= 1;
							else if (action_is_send(buf)) sel |= 2;
							else if (action_is_none(buf)) sel |= 4;
						}
					}
					if (sel == 1)
					{
						CheckMenuItem(h2, IDM_2TO1, MF_CHECKED);
					}
					if (sel == 2)
					{
						CheckMenuItem(h2, IDM_1TO2, MF_CHECKED);
					}
					if (sel == 4)
					{
						CheckMenuItem(h2, IDM_NOACTION, MF_CHECKED);
					}

					// AD: Disable the diff/open menu options as appropriate
					if (selcount == 1)
					{
						if (localonly)
						{
							::EnableMenuItem(h2, IDM_DIFF, MF_GRAYED);
							::EnableMenuItem(h2, IDM_OPENREMOTE, MF_GRAYED);
						}
						else if (remoteonly)
						{
							::EnableMenuItem(h2, IDM_DIFF, MF_GRAYED);
							::EnableMenuItem(h2, IDM_OPENLOCAL, MF_GRAYED);
						}
					}
					else
					{
						::EnableMenuItem(h2, IDM_DIFF, MF_GRAYED);
						::EnableMenuItem(h2, IDM_OPENLOCAL, MF_GRAYED);
						::EnableMenuItem(h2, IDM_OPENREMOTE, MF_GRAYED);
					}

					int do_action_change = 0;
					int x = TrackPopupMenu(h2, TPM_RETURNCMD, p.x, p.y, 0, hwndDlg, NULL);
					switch (x)
					{
					case IDM_2TO1:
						if (localonly)
						{
							char buf[512];
							sprintf(buf, "Setting the action to " ACTION_RECV " will result in %d local file%s/folder%s being removed.\r\n"
								"If this is acceptable, select Yes. Otherwise, select No.", localonly, localonly == 1 ? "" : "s", localonly == 1 ? "" : "s");
							if (MessageBox(hwndDlg, buf, "PathSync Warning", MB_YESNO | MB_ICONQUESTION) == IDYES) do_action_change = 1;
						}
						else do_action_change = 1;
						break;
					case IDM_1TO2:
						if (remoteonly)
						{
							char buf[512];
							sprintf(buf, "Setting the action to " ACTION_SEND " will result in %d remote file%s/folder%s being removed.\r\n"
								"If this is acceptable, select Yes. Otherwise, select No.", remoteonly, remoteonly == 1 ? "" : "s", remoteonly == 1 ? "" : "s");
							if (MessageBox(hwndDlg, buf, "PathSync Warning", MB_YESNO | MB_ICONQUESTION) == IDYES) do_action_change = 2;
						}
						else do_action_change = 2;
						break;
					case IDM_NOACTION:
						do_action_change = 3;
						break;

					case IDM_DIFF:
					{
						char path[1024];
						GetPrivateProfileString("config", "difftool", "", path, sizeof(path), m_inifile);

						// If no diff application has been chosen, give the user the opportunity to do it now.
						if (strlen(path) == 0)
						{
							EnableWindow(g_dlg, FALSE);
							DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_DIFFTOOL), NULL, diffToolProc);
							EnableWindow(g_dlg, TRUE);
							SetFocus(g_dlg);

							GetPrivateProfileString("config", "difftool", "", path, sizeof(path), m_inifile);
						}

						// If path length is still zero, no point trying to perform the diff.
						if (strlen(path) == 0)
						{
							break;
						}

						char params[256];
						GetPrivateProfileString("config", "diffparams", "\"%1\" \"%2\"", params, sizeof(params), m_inifile);

						char buf[1024] = "";
						int sel = ListView_GetSelectionMark(m_listview);
						ListView_GetItemText(m_listview, sel, COL_FILENAME, buf, sizeof(buf));

						// replace '%1' with the local file
						char param1[2048] = "";
						char * p1 = strstr(params, "%1");
						if (p1)
						{
							strncat(param1, params, p1 - params);
							strcat(param1, m_curscanner_basepath[0].Get());
							strcat(param1, PREF_DIRSTR);
							strcat(param1, buf);
							strcat(param1, p1 + 2);
						}
						else
						{
							strcat(param1, params);
						}

						// replace '%2' with the remote file
						char param2[2048] = "";
						char * p2 = strstr(param1, "%2");
						if (p2)
						{
							strncat(param2, param1, p2 - param1);
							strcat(param2, m_curscanner_basepath[1].Get());
							strcat(param2, PREF_DIRSTR);
							strcat(param2, buf);
							strcat(param2, p2 + 2);
						}
						else
						{
							strcat(param2, param1);
						}

						SHELLEXECUTEINFO sei = { sizeof(sei) };
						sei.fMask = 0;
						sei.nShow = SW_SHOWNORMAL;
						sei.lpVerb = "open";
						sei.lpFile = path;
						sei.lpParameters = param2;
						ShellExecuteEx(&sei);
					}
					break;

					case IDM_OPENLOCAL:
					{
						char buf[1024] = "";
						int sel = ListView_GetSelectionMark(m_listview);
						ListView_GetItemText(m_listview, sel, COL_FILENAME, buf, sizeof(buf));

						WDL_String gs;
						gs.Set(m_curscanner_basepath[0].Get());
						gs.Append(PREF_DIRSTR);
						gs.Append(buf);

						SHELLEXECUTEINFO sei = { sizeof(sei) };
						sei.fMask = 0;
						sei.nShow = SW_SHOWNORMAL;
						sei.lpVerb = "open";
						sei.lpFile = gs.Get();
						ShellExecuteEx(&sei);
					}
					break;

					case IDM_OPENREMOTE:
					{
						char buf[1024] = "";
						int sel = ListView_GetSelectionMark(m_listview);
						ListView_GetItemText(m_listview, sel, COL_FILENAME, buf, sizeof(buf));

						WDL_String gs;
						gs.Set(m_curscanner_basepath[1].Get());
						gs.Append(PREF_DIRSTR);
						gs.Append(buf);

						SHELLEXECUTEINFO sei = { sizeof(sei) };
						sei.fMask = 0;
						sei.nShow = SW_SHOWNORMAL;
						sei.lpVerb = "open";
						sei.lpFile = gs.Get();
						ShellExecuteEx(&sei);
					}
					break;
					}
					if (do_action_change)
					{
						for (y = 0; y < count; y++)
						{
							if (ListView_GetItemState(m_listview, y, LVIS_SELECTED) & LVIS_SELECTED)
							{
								char buf[128];
								ListView_GetItemText(m_listview, y, COL_STATUS, buf, sizeof(buf));

								// AD: Select the appropriate 'descriptive' action to inform the user of creates/deletes.
								char *s = ACTION_NONE;
								if (do_action_change == 1)   !strcmp(buf, LOCAL_ONLY_STR) ? s = ACTION_RECV_DELETE
									: !strcmp(buf, REMOTE_ONLY_STR) ? s = ACTION_RECV_CREATE
									: s = ACTION_RECV;
								if (do_action_change == 2)   !strcmp(buf, LOCAL_ONLY_STR) ? s = ACTION_SEND_CREATE
									: !strcmp(buf, REMOTE_ONLY_STR) ? s = ACTION_SEND_DELETE
									: s = ACTION_SEND;
								ListView_SetItemText(m_listview, y, COL_ACTION, s);
							}
						}
						calcStats(hwndDlg);
					}
				}
				DestroyMenu(h);
			}
		}
	}
	break;
	case WM_TIMER:
		if (wParam == 32)
		{
			bool finished = false;
			static int in_timer;
			if (!in_timer)
			{
				in_timer = 1;
				unsigned int start_t = GetTickCount();
				while (GetTickCount() - start_t < 200)
				{
					if (m_comparing < 7)
					{
						int x;
						for (x = 0; x < 2; x++)
						{
							if (!(m_comparing&(2 << x)))
							{
								// update item
								char *ptr = m_curscanner[x].GetCurrentFN();
								if (strcmp(ptr, ".") && strcmp(ptr, ".."))
								{
									WDL_String relname;
									relname.Set(m_curscanner_relpath[x].Get());
									if (m_curscanner_relpath[x].Get()[0]) relname.Append(PREF_DIRSTR);
									relname.Append(ptr);
									int isdir = m_curscanner[x].GetCurrentIsDirectory();
									if (isdir) relname.Append(PREF_DIRSTR);

									if (!test_file_pattern(relname.Get(), isdir))
									{
										// do nothing
									}
									else if (isdir)
									{
										WDL_String *s = new WDL_String(m_curscanner_relpath[x].Get());
										if (m_curscanner_relpath[x].Get()[0]) s->Append(PREF_DIRSTR);
										s->Append(ptr);
										m_dirscanlist[x].Add(s);

										// BU changed this to show dirs being scanned, not files (wayyy faster)
										WDL_String str2("Scanning dir: ");
										str2.Append(s->Get());
										SetDlgItemText(hwndDlg, IDC_STATUS, str2.Get());
										g_numfilesindir = 0;

										// AD: Add the folder to the list, if folder syncing is enabled.
										if (g_syncfolders)
										{
											// Add the path to the list
											dirItem *di = new dirItem;
											di->relativeFileName.Set(relname.Get());

											di->fileSize = 0;
											di->lastWriteTime.dwLowDateTime = di->lastWriteTime.dwHighDateTime = 0;

											m_files[x].Add(di);
										}
									}
									else
									{
										dirItem *di = new dirItem;
										di->relativeFileName.Set(relname.Get());

										DWORD hw = 0;
										di->fileSize = m_curscanner[x].GetCurrentFileSize(&hw);
										di->fileSize |= (((WDL_UINT64)hw) << 32);

										m_curscanner[x].GetCurrentLastWriteTime(&di->lastWriteTime);

										m_files[x].Add(di);

										if (g_numfilesindir++ >= 64) { //BU only show every N files since we show dirs now
											g_numfilesindir = 0;  // reset count
											WDL_String str2("Scanning file: ");
											str2.Append(di->relativeFileName.Get());
											SetDlgItemText(hwndDlg, IDC_STATUS, str2.Get());
											//                        str2.Append("\n");
											//                        OutputDebugString(str2.Get());
										}
									}
								}

								if (m_curscanner[x].Next())
								{
									int success = 0;
									m_curscanner[x].Close();
									// done with this dir!
									while (m_dirscanlist[x].GetSize() > 0 && !success)
									{
										int i = m_dirscanlist[x].GetSize() - 1;
										WDL_String *str = m_dirscanlist[x].Get(i);
										m_curscanner_relpath[x].Set(str->Get());
										m_dirscanlist[x].Delete(i);
										delete str;

										WDL_String s(m_curscanner_basepath[x].Get());
										s.Append(PREF_DIRSTR);
										s.Append(m_curscanner_relpath[x].Get());
										if (!m_curscanner[x].First(s.Get())) success++;
									}
									if (!success) m_comparing |= 2 << x;
								}
							}
						} // each dir
					} // < 7
					else if (m_comparing == 7) // sort 1
					{
						if (m_files[0].GetSize() > 1)
							qsort(m_files[0].GetList(), m_files[0].GetSize(), sizeof(dirItem *), (int(*)(const void*, const void*))filenameCompareFunction);
						m_comparing++;
					}
					else if (m_comparing == 8) // sort 2!
					{
						// this isn't really necessary, but it's fast and then provides consistent output for the ordering
						if (m_files[1].GetSize() > 1)
							qsort(m_files[1].GetList(), m_files[1].GetSize(), sizeof(dirItem *), (int(*)(const void*, const void*))filenameCompareFunction);
						m_comparing++;
						m_comparing_pos = 0;
						m_comparing_pos2 = 0;
					}
					else if (m_comparing == 9) // search m_files[0] for every m_files[1], reporting missing and different files
					{
						if (!m_files[1].GetSize())
						{
							m_comparing++;
						}
						else
						{
							dirItem **p = m_files[1].GetList() + m_comparing_pos;

							dirItem **res = 0;
							if (m_files[0].GetSize() > 0) res = (dirItem **)bsearch(p, m_files[0].GetList(), m_files[0].GetSize(), sizeof(dirItem *), (int(*)(const void*, const void*))filenameCompareFunction);

							if (!res)
							{
								if (!(g_ignflags & 4))
								{
									int x = ListView_GetItemCount(m_listview);
									LVITEM lvi = { LVIF_PARAM | LVIF_TEXT, x };
									lvi.pszText = (*p)->relativeFileName.Get();
									lvi.lParam = m_listview_recs.GetSize();
									m_listview_recs.Add(NULL);
									m_listview_recs.Add(*p);
									ListView_InsertItem(m_listview, &lvi);
									ListView_SetItemText(m_listview, x, COL_STATUS, REMOTE_ONLY_STR);
									ListView_SetItemText(m_listview, x, COL_ACTION,
										g_defbeh == 3 ? ACTION_SEND_DELETE : g_defbeh == 1 ? ACTION_NONE : ACTION_RECV_CREATE
										);

									// AD: don't set size value for folders
									if (!isDirectory(lvi.pszText))
									{
										// no local size
										char tmp[1024];
										format_size_string((*p)->fileSize, tmp);//BU
										ListView_SetItemText(m_listview, x, COL_REMOTESIZE, tmp);//BU
									}
								}
							}
							else
							{
								(*res)->refcnt++;
								WDL_UINT64 fta = *(WDL_UINT64 *)&(*p)->lastWriteTime;
								WDL_UINT64 ftb = *(WDL_UINT64 *)&(*res)->lastWriteTime;
								WDL_INT64 datediff = fta - ftb;
								if (datediff < 0) datediff = -datediff;

								// int dateMatch = datediff < 10000000 || (g_ignflags & 2); // if difference is less than 1s, they are equal
								// 10/24/2006 J. Puhlmann
								// Changed date diff to 2s, due to FAT limitation (FAT can apparently only store file times in 2s intervals,
								// this leads to problems when synchronizing between FAT and NTFS drives with some files.
								int dateMatch = datediff < 20000000 || (g_ignflags & 2); // if difference is less than 1s, they are equal

								if (!dateMatch && (g_ignflags & 16))
								{
									int y;
									for (y = 1; y <= 2 && !dateMatch; y++) // if 1 or 2 hours off, precisely, then DST related nonsense
									{
										WDL_INT64 l = datediff - y * 36000000000i64;
										dateMatch = l < 10000000 && l > -10000000;
									}
								}

								// BU added this so files with no dates will match up on NTFS vs FAT32
								if ((fta >> 32) <= 0x01a8e7f0 && (ftb >> 32) <= 0x01a8e7f0) { // both are pre 1980
									dateMatch = 1;
								}

								int sizeMatch = ((*p)->fileSize == (*res)->fileSize) || (g_ignflags & 1);
								if (!sizeMatch || !dateMatch)
								{
									int x = ListView_GetItemCount(m_listview);
									int insertpos = m_comparing_pos2++;
									LVITEM lvi = { LVIF_PARAM | LVIF_TEXT, insertpos };
									lvi.pszText = (*p)->relativeFileName.Get();
									lvi.lParam = m_listview_recs.GetSize();
									m_listview_recs.Add(*res);
									m_listview_recs.Add(*p);
									ListView_InsertItem(m_listview, &lvi);
									char *datedesc = 0, *sizedesc = 0;

									if (!dateMatch)
									{
										if (fta > ftb)
										{
											datedesc = "Remote Newer";
										}
										else
										{
											datedesc = "Local Newer";
										}
									}
									if (!sizeMatch)
									{
										if ((*p)->fileSize > (*res)->fileSize)
										{
											sizedesc = "Remote Larger";
										}
										else
										{
											sizedesc = "Local Larger";
										}
									}
									char buf[512];
									if (sizedesc && datedesc)
										sprintf(buf, "%s, %s", datedesc, sizedesc);
									else
										strcpy(buf, datedesc ? datedesc : sizedesc);

									ListView_SetItemText(m_listview, insertpos, COL_STATUS, buf);

									if (g_defbeh == 1 || g_defbeh == 3)
									{
										ListView_SetItemText(m_listview, insertpos, COL_ACTION, ACTION_SEND);
									}
									else if (g_defbeh == 2 || g_defbeh == 4)
									{
										ListView_SetItemText(m_listview, insertpos, COL_ACTION, ACTION_RECV);
									}
									else
										ListView_SetItemText(m_listview, insertpos, COL_ACTION,
										dateMatch ? ((*p)->fileSize > (*res)->fileSize ? ACTION_RECV : ACTION_SEND) :
										fta > ftb ? ACTION_RECV : ACTION_SEND);
									char tmp[1024]; format_size_string((*res)->fileSize, tmp);//BU
									ListView_SetItemText(m_listview, insertpos, COL_LOCALSIZE, tmp);//BU local size
									format_size_string((*p)->fileSize, tmp);//BU
									ListView_SetItemText(m_listview, insertpos, COL_REMOTESIZE, tmp);//BU remote size
								}
							}

							m_comparing_pos++;
							if (m_comparing_pos >= m_files[1].GetSize())
							{
								m_comparing++;
								m_comparing_pos = 0;
							}
						}
					}
					else if (m_comparing == 10) // scan for files in [0] that havent' been referenced
					{
						if (m_files[0].GetSize() < 1)
						{
							m_comparing++;
						}
						// at this point, we go through m_files[0] and search m_files[1] for files not 
						else
						{
							dirItem **p = m_files[0].GetList() + m_comparing_pos;

							if (!(*p)->refcnt)
							{
								if (!(g_ignflags & 8))
								{
									int x = ListView_GetItemCount(m_listview);
									LVITEM lvi = { LVIF_PARAM | LVIF_TEXT, x };
									lvi.pszText = (*p)->relativeFileName.Get();
									lvi.lParam = m_listview_recs.GetSize();
									m_listview_recs.Add(*p);
									m_listview_recs.Add(NULL);
									ListView_InsertItem(m_listview, &lvi);
									ListView_SetItemText(m_listview, x, COL_STATUS, LOCAL_ONLY_STR);

									ListView_SetItemText(m_listview, x, 2,
										g_defbeh == 4 ? ACTION_RECV_DELETE : g_defbeh == 2 ? ACTION_NONE : ACTION_SEND_CREATE
										);

									// AD: don't set size value for folders
									if (!isDirectory(lvi.pszText))
									{
										char tmp[1024]; format_size_string((*p)->fileSize, tmp);//BU
										ListView_SetItemText(m_listview, x, COL_LOCALSIZE, tmp);//local size only BU
									}
								}
							}

							m_comparing_pos++;
							if (m_comparing_pos >= m_files[0].GetSize())
							{
								m_comparing++;
								m_comparing_pos = 0;
							}
						}
					}
					else if (m_comparing == 11)
					{
						KillTimer(hwndDlg, 32);
						SetDlgItemText(hwndDlg, IDC_ANALYZE, "Analyze!");
						SetDlgItemText(hwndDlg, IDC_STATUS, "Status: Done");
						m_comparing = 0;
						enableAllButtonsInList(hwndDlg);
						EnableWindow(GetDlgItem(hwndDlg, IDC_GO), 1);
						calcStats(hwndDlg);

						free_pattern_list(&m_include_files);
						systray_mod(hwndDlg, 0, "PathSync");
						g_lasttraypercent = -1;

						finished = true;
						break; // exit loop
					}
				} // while
				in_timer = 0;
			} // if (!in_timer)

			if (finished && g_autorun)
			{
				if (IsWindowEnabled(GetDlgItem(hwndDlg, IDC_GO)))
				{
					PostMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_GO, 1), (LPARAM)GetDlgItem(hwndDlg, IDC_GO));
				}
				else if (g_autorun)
				{
					PostQuitMessage(1);
				}
			}

		} // (wParam == 32)
		break;
	}

	return 0;
}

char * skip_root(char *path)
{
#ifdef _WIN32
	char *p = (path + 1);
	char *p2 = (p + 1);

	if (*path && *p == ':' && *p2 == '\\')
	{
		return (p2 + 1);
	}
	else if (*path == '\\' && *p == '\\')
	{
		// skip host and share name
		int x = 2;
		while (x--)
		{
			while (*p2 != '\\')
			{
				if (!*p2)
					return NULL;
				p2 = (p2 + 1);
			}
			p2 = (p2 + 1);
		}

		return p2;
}
#else
	if (*path == '/') return path + 1;
#endif

	return NULL; // no root path found?!
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nShowCmd)
{
#ifdef _WIN32
	if (GetVersion() < 0x80000000)
	{
		LPSTR s = GetCommandParametersUTF8();
		if (s) lpszCmdParam = s;
	}
#endif

	g_hInstance = hInstance;
	InitCommonControls();

	GetModuleFileName(hInstance, m_inifile, sizeof(m_inifile) - 32);
	strcat(m_inifile, ".ini");

	{
		int state = 0;
		char *p = lpszCmdParam;
		while (*p)
		{
			char parm[2048];
			int parm_pos = 0, qs = 0;

			while (isspace(*p)) p++;
			if (!*p) break;

			while (*p && (!isspace(*p) || qs))
			{
				if (*p == '\"') qs = !qs;
				else if (parm_pos < (int)sizeof(parm) - 1) parm[parm_pos++] = *p;
				p++;
			}
			parm[parm_pos] = 0;

			if (parm[0] == '/') parm[0] = '-';
			switch (state)
			{
			case 0:
				if (!stricmp(parm, "-loadpss"))
				{
					state = 1;
				}
				else if (!stricmp(parm, "-autorun"))
				{
					g_autorun = true;
				}
				else if (!stricmp(parm, "-systray"))
				{
					g_systray = true;
				}
				else
				{
					state = -1;
				}
				break;
			case 1:
				if (parm[0] == '-') state = -1;
				else
				{
					lstrcpyn(g_loadsettingsfile, parm, sizeof(g_loadsettingsfile));
					state = 0;
				}
				break;
			}
			if (state < 0) break;
		}
		if (state)
		{
			MessageBox(NULL, "Usage:\r\npathsync [-loadpss <filename> [-autorun]]", "PathSync Usage", MB_OK);
			return 0;
		}
	}

	// fg, 4/20/2005, changed from DialogBox to CreateDialogBox + messagequeue in order to be able to start the dialog hidden
	g_dlg = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, mainDlgProc);
	if (!g_systray) { ShowWindow(g_dlg, SW_NORMAL); }
	else g_intray = true;

	MSG msg;
	while (GetMessage(&msg, (HWND)NULL, 0, 0))
	{
		if (IsDialogMessage(g_dlg, &msg)) continue;
		if (g_copydlg && IsDialogMessage(g_copydlg, &msg)) continue;
		// should never get here, but this code is there in case future extentions use CreateWindow*
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Calls WM_DESTROY, saves settings
	DestroyWindow(g_dlg);

	return 0;
}


//////////// file copying code, eh

int m_copy_entrypos;
int m_copy_done;
int m_copy_deletes, m_copy_files;
unsigned int m_copy_starttime;
WDL_INT64 m_copy_bytestotalsofar;

class fileCopier
{
public:
	fileCopier()
	{
		m_filepos = 0;
		m_filesize = 0;
		m_srcFile = 0;
		m_dstFile = 0;
		m_stt = GetTickCount();
		m_nud = 0;
	}
	int openFiles(char *src, char *dest, HWND hwndParent, char *relfn) // returns 1 on error
	{
		m_fullsrcfn.Set(src);
		m_fulldestfn.Set(dest);
		m_relfn.Set(relfn);

#define BIG_BUFSIZE (1024*1024)
		m_srcFile = new WDL_FileRead(src, 1, BIG_BUFSIZE, 8);
		if (!m_srcFile->IsOpen())
		{
			delete m_srcFile;
			m_srcFile = 0;

			WDL_String tmp("Error opening source: ");
			tmp.Append(src);

			LogMessage(tmp.Get());
			SendDlgItemMessage(hwndParent, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)tmp.Get());
			return -1;
		}

		createdir(dest);

		m_tmpdestfn.Set(dest);
		m_tmpdestfn.Append(".PSYN_TMP");
		m_dstFile = new WDL_FileWrite(m_tmpdestfn.Get(), 0, BIG_BUFSIZE); // sync writes

		if (!m_dstFile->IsOpen())
		{
			WDL_String tmp("Error opening tmpdest: ");
			tmp.Append(m_tmpdestfn.Get());

			LogMessage(tmp.Get());
			SendDlgItemMessage(hwndParent, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)tmp.Get());
			return -1;
		}

		m_filesize = m_srcFile->GetSize();
		m_filepos = 0;

		SendDlgItemMessage(hwndParent, IDC_FILEPROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 10000));
		SendDlgItemMessage(hwndParent, IDC_FILEPROGRESS, PBM_SETPOS, 0, 0);
		SetDlgItemText(hwndParent, IDC_FILEPOS, "Copying...");

		return 0;
	}

	// AD: Refactored directory creation into it's own method for reuse.
	BOOL createdir(char * dest)
	{
		BOOL success = FALSE;
		WDL_String tmp(dest);
		char *p = tmp.Get();
		if (*p)
		{
			p = skip_root(tmp.Get());
			if (p) for (;;)
			{
				while (*p && *p != '\\' && *p != '/') p++;
				if (!*p) break;

				char c = *p;
				*p = 0;
				success &= CreateDirectory(tmp.Get(), NULL);
				*p++ = c;
			}
		}
		return success;
	}

	int run(HWND hwndParent) // return 1 when done
	{
		static char buf[128 * 1024];
		DWORD r = m_srcFile->Read(buf, sizeof(buf));
		if (!r && m_srcFile->GetPosition() < m_srcFile->GetSize())
		{
			WDL_String tmp("Error reading: ");
			tmp.Append(m_relfn.Get());
			LogMessage(tmp.Get());
			SendDlgItemMessage(hwndParent, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)tmp.Get());
			return -1;
		}

		if (r)
		{
			m_copy_bytestotalsofar += r;
			m_filepos += r;
			g_throttle_bytes += r;

			DWORD or = m_dstFile->Write(buf, r);
			if (or != r)
			{
				WDL_String tmp("Error writing to: ");
				tmp.Append(m_relfn.Get());
				LogMessage(tmp.Get());
				SendDlgItemMessage(hwndParent, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)tmp.Get());
				return -1;
			}
		}

		unsigned int now = GetTickCount();
		if (now > m_nud || r < sizeof(buf))
		{
			m_nud = now + 100;
			int v = 0;
			unsigned int tm = now - m_stt;
			if (!tm) tm = 1;
			if (m_filesize) v = (int)((m_filepos * 10000) / m_filesize);
			SendDlgItemMessage(hwndParent, IDC_FILEPROGRESS, PBM_SETPOS, (WPARAM)v, 0);
			{
				char text[512];
				char tmp1[128], tmp2[128], tmp3[128];
				format_size_string(m_filepos, tmp1);
				format_size_string(m_filesize, tmp2);
				format_size_string((m_filepos * 1000) / tm, tmp3);

				sprintf(text, "%d%% - %s/%s @ %s/s", v / 100, tmp1, tmp2, tmp3);
				SetDlgItemText(hwndParent, IDC_FILEPOS, text);
			}
		}

		if (r < sizeof(buf)) // eof!
		{
			if (m_filesize < 16384) g_throttle_bytes += 16384;
			FILETIME ft;
			GetFileTime(m_srcFile->GetHandle(), NULL, NULL, &ft);
			SetFileTime(m_dstFile->GetHandle(), NULL, NULL, &ft);
			delete m_srcFile;
			m_srcFile = 0;
			delete m_dstFile;
			m_dstFile = 0;


			WDL_String destSave(m_fulldestfn.Get());
			destSave.Append(".PSYN_OLD");
			int err = 0;

			bool fileExists;

			{
				WDL_FileRead hFE(m_fulldestfn.Get(), 0);
				fileExists = hFE.IsOpen();
			}

			if (!fileExists || MoveFile(m_fulldestfn.Get(), destSave.Get()))
			{
				if (MoveFile(m_tmpdestfn.Get(), m_fulldestfn.Get()))
				{
					if (fileExists) DeleteFile(destSave.Get());
				}
				else
				{
					if (fileExists) MoveFile(destSave.Get(), m_fulldestfn.Get()); // try and restore old
					err = 2;
				}
			}
			else err = 1;

			if (err)
			{
				WDL_String tmp("Error finalizing: ");
				tmp.Append(m_relfn.Get());
				LogMessage(tmp.Get());
				SendDlgItemMessage(hwndParent, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)tmp.Get());
				return -1;
			}
			else
			{
				char text[2048] = "";
				unsigned int tm = max(now - m_stt, 1);
				char tmp2[128], tmp3[128];

				format_size_string(m_filesize, tmp2);
				format_size_string((m_filepos * 1000) / tm, tmp3);
				sprintf(text, "%s @ %s/s %s", tmp2, tmp3, m_fulldestfn.Get());
				LogMessage(text);
			}

			// close handles, delete/rename files
			m_copy_files++;
			return 1;
		}

		return 0;
	}

	~fileCopier()
	{
		if (m_dstFile)
		{
			delete m_dstFile;
			DeleteFile(m_tmpdestfn.Get());
		}
		delete m_srcFile;

	}

	WDL_String m_tmpdestfn;

	WDL_String m_fullsrcfn, m_fulldestfn, m_relfn;
	WDL_INT64 m_filepos;
	WDL_INT64 m_filesize;
	WDL_FileRead *m_srcFile;
	WDL_FileWrite *m_dstFile;
	unsigned int m_stt, m_nud;
};
fileCopier *m_copy_curcopy;
unsigned int m_next_statusupdate;

void updateXferStatus(HWND hwndDlg)
{
	char buf[512];
	char *p = buf;
	unsigned int t = GetTickCount() - m_copy_starttime;
	if (!t) t = 1;
	if (!m_total_copy_size) m_total_copy_size = 1;
	int v = (int)((m_copy_bytestotalsofar * 10000) / m_total_copy_size);


	WDL_INT64 bytesleft = m_total_copy_size - m_copy_bytestotalsofar;
	int pred_t = 0;
	if (m_copy_bytestotalsofar) pred_t = (int)((t / 1000) * m_total_copy_size / m_copy_bytestotalsofar);

	SendDlgItemMessage(hwndDlg, IDC_TOTALPROGRESS, PBM_SETPOS, v, 0);
	char tmp1[128], tmp2[128], tmp3[128];
	format_size_string(m_copy_bytestotalsofar, tmp1);
	format_size_string(m_total_copy_size, tmp2);
	format_size_string((m_copy_bytestotalsofar * 1000) / t, tmp3);

	sprintf(buf, "%d%% - %d file%s/folder%s (%s/%s) copied at %s/s, %d file%s/folder%s deleted.\r\nElapsed Time: %d:%02d, Time Remaining: %d:%02d", v / 100,
		m_copy_files, m_copy_files == 1 ? "" : "s", m_copy_files == 1 ? "" : "s",
		tmp1,
		tmp2,
		tmp3,
		m_copy_deletes, m_copy_deletes == 1 ? "" : "s", m_copy_deletes == 1 ? "" : "s",
		t / 60000, (t / 1000) % 60,
		(pred_t - t / 1000) / 60, (pred_t - t / 1000) % 60);
	SetDlgItemText(hwndDlg, IDC_TOTALPOS, p);
	if (g_intray)
	{
		if (v / 100 != g_lasttraypercent)
		{
			sprintf(buf, "PathSync - Synchronizing - %d%%", v / 100);
			systray_mod(g_dlg, 0, buf);
			g_lasttraypercent = v / 100;
		}
	}
}

void LogEndSyncMessage()
{
	char buf[512];
	unsigned int t = GetTickCount() - m_copy_starttime;
	char tmp1[128], tmp2[128];

	format_size_string(m_total_copy_size, tmp1);
	format_size_string(t ? (m_total_copy_size * 1000) / t : 0, tmp2);

	sprintf(buf, "%d file%s copied (%s @ %s/s), %d file%s deleted. Elapsed Time: %d:%02d",
		m_copy_files,
		m_copy_files == 1 ? "" : "s",
		tmp1,
		tmp2,
		m_copy_deletes,
		m_copy_deletes == 1 ? "" : "s",
		t / 60000,
		(t / 1000) % 60);

	LogMessage(buf);
}

BOOL WINAPI copyFilesProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static WDL_WndSizer resizer;
	switch (uMsg)
	{
	case WM_INITDIALOG:

		resizer.init(hwndDlg);
		resizer.init_item(IDC_SRC, 0, 0, 1, 0);
		resizer.init_item(IDC_DEST, 0, 0, 1, 0);
		resizer.init_item(IDC_FILEPOS, 0, 0, 1, 0);
		resizer.init_item(IDC_FILEPROGRESS, 0, 0, 1, 0);
		resizer.init_item(IDC_TOTALPOS, 0, 0, 1, 0);
		resizer.init_item(IDC_TOTALPROGRESS, 0, 0, 1, 0);

		resizer.init_item(IDC_LIST1, 0, 0, 1, 1);
		resizer.init_item(IDC_CHECK2, 0, 1, 0, 1);
		resizer.init_item(IDC_EDIT1, 0, 1, 0, 1);
		resizer.init_item(IDC_THROTTLELBL, 0, 1, 0, 1);
		resizer.init_item(IDC_CHECK1, 0, 1, 0, 1);
		resizer.init_item(IDCANCEL, 1, 1, 1, 1);

		if (GetPrivateProfileInt("config", "accopy", 0, m_inifile)) CheckDlgButton(hwndDlg, IDC_CHECK1, BST_CHECKED);
		if (g_throttle) CheckDlgButton(hwndDlg, IDC_CHECK2, BST_CHECKED);

		// IDC_LIST1 is a list box, not a listview, so don't do this.
		//      WDL_UTF8_HookListView(GetDlgItem(hwndDlg,IDC_LIST1));

		SetDlgItemInt(hwndDlg, IDC_EDIT1, g_throttlespd, FALSE);
		m_copy_starttime = GetTickCount();
		m_next_statusupdate = 0;
		m_copy_deletes = m_copy_files = 0;
		m_copy_curcopy = 0;
		m_copy_entrypos = -1;
		m_copy_done = 0;
		m_copy_bytestotalsofar = 0;
		SendDlgItemMessage(hwndDlg, IDC_TOTALPROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 10000));
		SendDlgItemMessage(hwndDlg, IDC_TOTALPROGRESS, PBM_SETPOS, 0, 0);
		SetTimer(hwndDlg, 60, 50, NULL);

		g_throttle_sttime = GetTickCount();
		g_throttle_bytes = 0;

		return 0;
	case WM_DESTROY:
		if (m_copy_curcopy)
		{
			delete m_copy_curcopy;
			// clean up after copy
			m_copy_curcopy = 0;
		}
		return 0;
	case WM_TIMER:
		if (wParam == 60)
		{
			unsigned int start_t = GetTickCount();
			unsigned int now;
			while ((now = GetTickCount()) - start_t < 150)
			{
				if (g_throttle)
				{
					DWORD now = GetTickCount();
					if (now < g_throttle_sttime || now > g_throttle_sttime + 30000)
					{
						g_throttle_sttime = now;
						g_throttle_bytes = 0;
					}
					now -= g_throttle_sttime;
					if (!now) now = 1;
					int kbytes_sec = (int)(g_throttle_bytes / now);
					if (kbytes_sec > g_throttlespd)
						break; // wait til next WM_TIMER
				}

				if (m_copy_curcopy && m_copy_curcopy->run(hwndDlg))
				{
					// if copy finishes, reset 
					delete m_copy_curcopy;
					m_copy_curcopy = 0;
				}

				if (now > m_next_statusupdate)
				{
					updateXferStatus(hwndDlg);
					m_next_statusupdate = now + 1000;
				}


				if (!m_copy_curcopy)
				{
					m_copy_entrypos++;
					m_next_statusupdate = 0;

					if (m_copy_entrypos >= ListView_GetItemCount(m_listview))
					{
						updateXferStatus(hwndDlg);
						SetDlgItemText(hwndDlg, IDC_SRC, "");
						SetDlgItemText(hwndDlg, IDC_DEST, "");
						KillTimer(hwndDlg, 60);
						m_copy_done = 1;
						SetDlgItemText(hwndDlg, IDC_FILEPOS, "");
						SetDlgItemText(hwndDlg, IDCANCEL, "Close");
						if (IsDlgButtonChecked(hwndDlg, IDC_CHECK1) || g_autorun)
						{
							PostMessage(g_dlg, WM_COPYDIALOGEND, 1, 0);
						}
						LogEndSyncMessage();
						return 0;
					}
					else
					{
						char status[256];
						char action[256];
						char filename[2048];
						ListView_GetItemText(m_listview, m_copy_entrypos, 0, filename, sizeof(filename));
						ListView_GetItemText(m_listview, m_copy_entrypos, 1, status, sizeof(status));
						ListView_GetItemText(m_listview, m_copy_entrypos, 2, action, sizeof(action));

						// AD: Use wrappers to compare
						int isSend = action_is_send(action);
						int isRecv = action_is_recv(action);

						if ((isRecv && !strcmp(status, LOCAL_ONLY_STR)) ||
							(isSend && !strcmp(status, REMOTE_ONLY_STR)))
						{
							SetDlgItemText(hwndDlg, IDC_SRC, "<delete>");
							WDL_String gs;
							gs.Set(m_curscanner_basepath[!isRecv].Get());
							gs.Append(PREF_DIRSTR);
							gs.Append(filename);
							SetDlgItemText(hwndDlg, IDC_DEST, gs.Get());

							// AD: Test for file or folder is to be deleted.
							if (isDirectory(filename))
							{
								if (!RemoveDirectory(gs.Get()))
								{
									WDL_String news("Error removing");
									news.Append(isRecv ? " local folder: " : " remote folder: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
									SendDlgItemMessage(hwndDlg, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)news.Get());
								}
								else
								{
									m_copy_deletes++;
									WDL_String news("Removed");
									news.Append(isRecv ? " local folder: " : " remote folder: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
								}
							}
							else
							{
								if (!DeleteFile(gs.Get()))
								{
									WDL_String news("Error removing ");
									news.Append(isRecv ? "local file: " : "remote file: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
									SendDlgItemMessage(hwndDlg, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)news.Get());
								}
								else
								{
									m_copy_deletes++;
									WDL_String news("Removed ");
									news.Append(isRecv ? "local file: " : "remote file: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
								}
							}
						}
						else if (isRecv || isSend)
						{
							WDL_String gs;
							gs.Set(m_curscanner_basepath[!!isRecv].Get());
							gs.Append(PREF_DIRSTR);
							gs.Append(filename);
							SetDlgItemText(hwndDlg, IDC_SRC, gs.Get());

							WDL_String outgs;
							outgs.Set(m_curscanner_basepath[!isRecv].Get());
							outgs.Append(PREF_DIRSTR);
							outgs.Append(filename);
							SetDlgItemText(hwndDlg, IDC_DEST, outgs.Get());

							m_copy_curcopy = new fileCopier;

							// AD: Test whether file or folder to be created/copied
							if (isDirectory(filename))
							{
								// Create the directory
								if (m_copy_curcopy->createdir(outgs.Get()))
								{
									WDL_String news("Error creating ");
									news.Append(isRecv ? "local folder: " : "remote folder: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
									SendDlgItemMessage(hwndDlg, IDC_LIST1, LB_ADDSTRING, 0, (LPARAM)news.Get());
								}
								else
								{
									m_copy_files++;
									WDL_String news("Created ");
									news.Append(isRecv ? "local folder: " : "remote folder: ");
									news.Append(gs.Get());
									LogMessage(news.Get());
								}

								delete m_copy_curcopy;
								m_copy_curcopy = 0;
							}
							else if (m_copy_curcopy->openFiles(gs.Get(), outgs.Get(), hwndDlg, filename))
							{
								// add error string according to x.
								delete m_copy_curcopy;
								m_copy_curcopy = 0;
							}
						}

						// start new copy
					}
				}
			} // while < 100ms
		} // if 60
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_CHECK1:
			WritePrivateProfileString("config", "accopy", IsDlgButtonChecked(hwndDlg, IDC_CHECK1) ? "1" : "0", m_inifile);
			break;
		case IDC_CHECK2:
			g_throttle = !!IsDlgButtonChecked(hwndDlg, IDC_CHECK2);
			g_throttle_sttime = GetTickCount();
			g_throttle_bytes = 0;
			break;
		case IDC_EDIT1:
			if (HIWORD(wParam) == EN_CHANGE)
			{
				BOOL t = 0;
				int a = GetDlgItemInt(hwndDlg, IDC_EDIT1, &t, FALSE);
				if (t)
				{
					g_throttlespd = a;
					g_throttle_sttime = GetTickCount();
					g_throttle_bytes = 0;
				}
			}
			break;
		case IDCANCEL:
			if (m_copy_done || MessageBox(hwndDlg, "Cancel synchronization?", "Question", MB_YESNO) == IDYES)
				PostMessage(g_dlg, WM_COPYDIALOGEND, 1, 0);
			break;
		}
		break;
	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO p = (LPMINMAXINFO)lParam;
		p->ptMinTrackSize.x = 430;
		p->ptMinTrackSize.y = 258;
	}
	return 0;

	case WM_SIZE:
		if (g_systray)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				g_intray = true;
				ShowWindow(hwndDlg, SW_HIDE);
			}
			else if (wParam == SIZE_RESTORED)
			{
				g_intray = false;
				ShowWindow(hwndDlg, SW_SHOW);
			}
		}
		else
		{
			if (wParam != SIZE_MINIMIZED) {
				resizer.onResize();
			}
		}
		return 0;
	}
	return 0;
}

BOOL WINAPI diffToolProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		char path[1024];
		GetPrivateProfileString("config", "difftool", "", path, sizeof(path), m_inifile);
		SetDlgItemText(hwndDlg, IDC_DIFF, path);
		char params[1024];
		GetPrivateProfileString("config", "diffparams", "\"%1\" \"%2\"", params, sizeof(params), m_inifile);
		SetDlgItemText(hwndDlg, IDC_DIFFPARAMS, params);
	}
	break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BROWSE:
		{
			char path[1024] = "";
			GetDlgItemText(hwndDlg, IDC_DIFF, path, sizeof path);
			OPENFILENAME ofn = { 0 };
			ofn.lStructSize = sizeof OPENFILENAME;
			ofn.hwndOwner = hwndDlg;
			ofn.lpstrTitle = "Choose a Diff Tool";
			ofn.lpstrFilter = "Application (.exe)\0*.exe\0All Files (*.*)\0*.*\0";
			ofn.lpstrFile = path;
			ofn.nMaxFile = sizeof path;

			if (GetOpenFileName(&ofn))
			{
				SetDlgItemText(hwndDlg, IDC_DIFF, path);
			}
		}
		break;

		case IDOK:
		{
			char path[1024] = "";
			GetDlgItemText(hwndDlg, IDC_DIFF, path, sizeof path);
			WritePrivateProfileString("config", "difftool", path, m_inifile);
			char params[1024] = "";
			char escapedparams[1024] = "";
			GetDlgItemText(hwndDlg, IDC_DIFFPARAMS, params, sizeof params);
			// AD: Wrap the string in quotes, as GetPrivateProfileString strips any leading and trailing quotes
			sprintf(escapedparams, "\"%s\"", params);
			WritePrivateProfileString("config", "diffparams", escapedparams, m_inifile);
			EndDialog(hwndDlg, 0);
		}
		break;

		case IDCANCEL:
			EndDialog(hwndDlg, 0);
			break;
		}
		break;
	}

	return 0;
}
