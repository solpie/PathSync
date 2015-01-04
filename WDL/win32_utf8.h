#ifndef _WDL_WIN32_UTF8_H_
#define _WDL_WIN32_UTF8_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(WDL_NO_SUPPORT_UTF8)

#ifndef WDL_WIN32_UTF8_IMPL 
#define WDL_WIN32_UTF8_IMPL
#define WDL_WIN32_UTF8_IMPL_NOTSTATIC
#endif

#include <windows.h>
#include <sys/stat.h>

WDL_WIN32_UTF8_IMPL BOOL SetWindowTextUTF8(HWND hwnd, LPCTSTR str);
WDL_WIN32_UTF8_IMPL BOOL SetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPCTSTR lpString);
WDL_WIN32_UTF8_IMPL int GetWindowTextUTF8(HWND hWnd, LPTSTR lpString, int nMaxCount);
WDL_WIN32_UTF8_IMPL UINT GetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPTSTR lpString, int nMaxCount);

WDL_WIN32_UTF8_IMPL BOOL CreateDirectoryUTF8(LPCTSTR path, LPSECURITY_ATTRIBUTES attr);
WDL_WIN32_UTF8_IMPL BOOL DeleteFileUTF8(LPCTSTR path);
WDL_WIN32_UTF8_IMPL BOOL MoveFileUTF8(LPCTSTR existfn, LPCTSTR newfn);
WDL_WIN32_UTF8_IMPL DWORD GetCurrentDirectoryUTF8(DWORD nBufferLength, LPTSTR lpBuffer);
WDL_WIN32_UTF8_IMPL BOOL SetCurrentDirectoryUTF8(LPCTSTR path);
WDL_WIN32_UTF8_IMPL HINSTANCE LoadLibraryUTF8(LPCTSTR path);

WDL_WIN32_UTF8_IMPL HANDLE CreateFileUTF8(LPCTSTR lpFileName,DWORD dwDesiredAccess,DWORD dwShareMode,LPSECURITY_ATTRIBUTES lpSecurityAttributes,DWORD dwCreationDisposition,DWORD dwFlagsAndAttributes,HANDLE hTemplateFile);

WDL_WIN32_UTF8_IMPL int DragQueryFileUTF8(HDROP hDrop, int idx, char *buf, int bufsz);

WDL_WIN32_UTF8_IMPL int DrawTextUTF8(HDC hdc, LPCTSTR str, int nc, LPRECT lpRect, UINT format);

WDL_WIN32_UTF8_IMPL BOOL GetOpenFileNameUTF8(LPOPENFILENAME lpofn);
WDL_WIN32_UTF8_IMPL BOOL GetSaveFileNameUTF8(LPOPENFILENAME lpofn);


WDL_WIN32_UTF8_IMPL BOOL InsertMenuItemUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii);
WDL_WIN32_UTF8_IMPL BOOL SetMenuItemInfoUTF8(HMENU hMenu, UINT uItem, BOOL fByPosition,LPMENUITEMINFO lpmii);
WDL_WIN32_UTF8_IMPL BOOL GetMenuItemInfoUTF8(HMENU hMenu, UINT uItem,BOOL fByPosition,LPMENUITEMINFO lpmii);
   
WDL_WIN32_UTF8_IMPL int statUTF8(const char *filename, struct stat *buffer);

WDL_WIN32_UTF8_IMPL WCHAR *WDL_UTF8ToWC(const char *buf, BOOL doublenull, int minsize, DWORD *sizeout); 

WDL_WIN32_UTF8_IMPL BOOL WDL_HasUTF8(const char *_str);

WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookComboBox(HWND h);
WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookListView(HWND h);
WDL_WIN32_UTF8_IMPL void WDL_UTF8_HookListBox(HWND h);

WDL_WIN32_UTF8_IMPL LPSTR GetCommandParametersUTF8();
WDL_WIN32_UTF8_IMPL void WDL_UTF8_ListViewConvertDispInfoToW(void *di); //NMLVDISPINFO 

#ifdef SetWindowText
#undef SetWindowText
#endif
#define SetWindowText SetWindowTextUTF8

#ifdef SetDlgItemText
#undef SetDlgItemText
#endif
#define SetDlgItemText SetDlgItemTextUTF8


#ifdef GetWindowText
#undef GetWindowText
#endif
#define GetWindowText GetWindowTextUTF8

#ifdef GetDlgItemText
#undef GetDlgItemText
#endif
#define GetDlgItemText GetDlgItemTextUTF8

#ifdef DragQueryFile
#undef DragQueryFile
#endif
#define DragQueryFile DragQueryFileUTF8

#ifdef GetOpenFileName
#undef GetOpenFileName
#endif
#define GetOpenFileName GetOpenFileNameUTF8

#ifdef GetSaveFileName
#undef GetSaveFileName
#endif
#define GetSaveFileName GetSaveFileNameUTF8


#ifdef CreateDirectory
#undef CreateDirectory
#endif
#define CreateDirectory CreateDirectoryUTF8

#ifdef DeleteFile
#undef DeleteFile
#endif
#define DeleteFile DeleteFileUTF8

#ifdef MoveFile
#undef MoveFile
#endif
#define MoveFile MoveFileUTF8

#ifdef GetCurrentDirectory
#undef GetCurrentDirectory
#endif
#define GetCurrentDirectory GetCurrentDirectoryUTF8

#define PathFileExists PathFileExists


#ifdef SetCurrentDirectory
#undef SetCurrentDirectory
#endif
#define SetCurrentDirectory SetCurrentDirectoryUTF8


#ifdef CreateFile
#undef CreateFile
#endif
#define CreateFile CreateFileUTF8


#ifdef InsertMenuItem
#undef InsertMenuItem
#endif
#define InsertMenuItem InsertMenuItemUTF8

#ifdef SetMenuItemInfo
#undef SetMenuItemInfo
#endif
#define SetMenuItemInfo SetMenuItemInfoUTF8

#ifdef GetMenuItemInfo
#undef GetMenuItemInfo
#endif
#define GetMenuItemInfo GetMenuItemInfoUTF8

#ifdef LoadLibrary
#undef LoadLibrary
#endif
#define LoadLibrary LoadLibraryUTF8

#else

// compat defines for when UTF disabled
#define DrawTextUTF8 DrawText
#define statUTF8 stat
#define WDL_UTF8_HookComboBox(x)
#define WDL_UTF8_HookListView(x)
#define WDL_UTF8_ListViewConvertDispInfoToW(x)

#endif

#ifdef __cplusplus
};
#endif

#endif