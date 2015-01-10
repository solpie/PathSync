#ifdef UNICODE
#define LVITEM    LVITEMW
#define LPLVITEM  LPLVITEMW
#define LVITEM_V1_SIZE LVITEMW_V1_SIZE
#else
#define LVITEM    LVITEMA
#define LPLVITEM  LPLVITEMA
#define LVITEM_V1_SIZE LVITEMA_V1_SIZE
#endif
#include "win32_utf8.h"
#include "wdltypes.h"
#ifdef _WIN32

#if !defined(WDL_NO_SUPPORT_UTF8)

#include <commctrl.h>

#ifdef __cplusplus
extern "C" {
#endif


#ifndef WDL_UTF8_MAXFNLEN
#define WDL_UTF8_MAXFNLEN 2048
#endif

#define MBTOWIDE(symbase, src) \
                int symbase##_size; \
                WCHAR symbase##_buf[1024]; \
                WCHAR *symbase = (symbase##_size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,NULL,0)) >= 1000 ? (WCHAR *)malloc(symbase##_size * sizeof(WCHAR) + 10) : symbase##_buf; \
                int symbase##_ok = symbase ? (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,symbase,symbase##_size < 1 ? 1024 : symbase##_size)) : 0

#define MBTOWIDE_FREE(symbase) if (symbase != symbase##_buf) free(symbase)


#define WIDETOMB_ALLOC(symbase, length) \
            WCHAR symbase##_buf[1024]; \
            int symbase##_size = sizeof(symbase##_buf); \
            WCHAR *symbase = length > 1000 ? (WCHAR *)malloc(symbase##_size = (sizeof(WCHAR)*length + 10)) : symbase##_buf

#define WIDETOMB_FREE(symbase) if (symbase != symbase##_buf) free(symbase)

BOOL WDL_HasUTF8(const char *_str)
{
  const unsigned char *str = (const unsigned char *)_str;
  if (!str) return FALSE;
  while (*str) 
  {
    unsigned char c = *str;
    if (c >= 0xC2) // fuck overlongs
    {
      if (c <= 0xDF && str[1] >=0x80 && str[1] <= 0xBF) return TRUE;
      else if (c <= 0xEF && str[1] >=0x80 && str[1] <= 0xBF && str[2] >=0x80 && str[2] <= 0xBF) return TRUE;
      else if (c <= 0xF4 && str[1] >=0x80 && str[1] <= 0xBF && str[2] >=0x80 && str[2] <= 0xBF) return TRUE;
    }
    str++;
  }
  return FALSE;
}

int GetWindowTextUTF8(HWND hWnd, LPTSTR lpString, int nMaxCount)
{
  if (lpString && nMaxCount>0 && GetVersion()< 0x80000000)
  {
    int alloc_size=nMaxCount;

    // prevent large values of nMaxCount from allocating memory unless the underlying text is big too
    if (alloc_size > 512)  
    {
      int l=GetWindowTextLengthW(hWnd);
      if (l>=0 && l < 512) alloc_size=1000;
    }

    {
      WIDETOMB_ALLOC(wbuf, alloc_size);
      if (wbuf)
      {
        GetWindowTextW(hWnd,wbuf,wbuf_size/sizeof(WCHAR));      

        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpString,nMaxCount,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
          lpString[nMaxCount-1]=0;

        WIDETOMB_FREE(wbuf);

        return strlen(lpString);
      }
    }
  }
  return GetWindowTextA(hWnd,lpString,nMaxCount);
}

UINT GetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPTSTR lpString, int nMaxCount)
{
  HWND h = GetDlgItem(hDlg,nIDDlgItem);
  if (h) return GetWindowTextUTF8(h,lpString,nMaxCount);
  return 0;
}


BOOL SetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPCTSTR lpString)
{
  HWND h = GetDlgItem(hDlg,nIDDlgItem);
  if (h) return SetWindowTextUTF8(h,lpString);
  return FALSE;
}

BOOL SetWindowTextUTF8(HWND hwnd, LPCTSTR str)
{
  if (WDL_HasUTF8(str) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,str);
    if (wbuf_ok)
    {
      BOOL rv=SetWindowTextW(hwnd,wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }

    MBTOWIDE_FREE(wbuf);
  }

  return SetWindowTextA(hwnd,str);
}

int DragQueryFileUTF8(HDROP hDrop, int idx, char *buf, int bufsz)
{
  if (buf && bufsz && idx!=-1 && GetVersion()< 0x80000000)
  {
    int reqsz = DragQueryFileW(hDrop,idx,NULL,0)+32;
    WIDETOMB_ALLOC(wbuf, reqsz);
    if (wbuf)
    {
      int rv=DragQueryFileW(hDrop,idx,wbuf,wbuf_size/sizeof(WCHAR));
      if (rv)
      {
        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,buf,bufsz,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
          buf[bufsz-1]=0;
      }
      WIDETOMB_FREE(wbuf);
      return rv;
    }
  }
  return DragQueryFileA(hDrop,idx,buf,bufsz);
}


WCHAR *WDL_UTF8ToWC(const char *buf, BOOL doublenull, int minsize, DWORD *sizeout)
{
  if (doublenull)
  {
    int sz=1;
    const char *p = (const char *)buf;
    WCHAR *pout,*ret;

    while (*p)
    {
      int a=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,-1,NULL,0);
      int sp=strlen(p)+1;
      if (a < sp)a=sp; // in case it needs to be ansi mapped
      sz+=a;
      p+=sp;
    }
    if (sz < minsize) sz=minsize;

    pout = (WCHAR *) malloc(sizeof(WCHAR)*(sz+4));
    ret=pout;
    p = (const char *)buf;
    while (*p)
    {
      int a;
      *pout=0;
      a = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,-1,pout,sz-(pout-ret));
      if (!a)
      {
        pout[0]=0;
        a=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,p,-1,pout,sz-(pout-ret));
      }
      pout += a;
      p+=strlen(p)+1;
    }
    *pout=0;
    pout[1]=0;
    if (sizeout) *sizeout=sz;
    return ret;
  }
  else
  {
    int srclen = strlen(buf)+1;
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,buf,srclen,NULL,0);
    if (size < srclen)size=srclen; // for ansi code page
    if (size<minsize)size=minsize;

    {
      WCHAR *outbuf = (WCHAR *)malloc(sizeof(WCHAR)*(size+128));
      int a;
      *outbuf=0;
      if (srclen>1)
      {
        a=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,buf,srclen,outbuf, size);
        if (!a)
        {
          outbuf[0]=0;
          a=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,buf,srclen,outbuf,size);
        }
      }
      if (sizeout) *sizeout = size;
      return outbuf;
    }
  }
}

static BOOL GetOpenSaveFileNameUTF8(LPOPENFILENAME lpofn, BOOL save)
{

  OPENFILENAMEW tmp={sizeof(tmp),lpofn->hwndOwner,lpofn->hInstance,};
  BOOL ret;

  // allocate, convert input
  if (lpofn->lpstrFilter) tmp.lpstrFilter = WDL_UTF8ToWC(lpofn->lpstrFilter,TRUE,0,0);
  tmp.nFilterIndex = lpofn->nFilterIndex ;

  if (lpofn->lpstrFile) tmp.lpstrFile = WDL_UTF8ToWC(lpofn->lpstrFile,FALSE,lpofn->nMaxFile,&tmp.nMaxFile);
  if (lpofn->lpstrFileTitle) tmp.lpstrFileTitle = WDL_UTF8ToWC(lpofn->lpstrFileTitle,FALSE,lpofn->nMaxFileTitle,&tmp.nMaxFileTitle);
  if (lpofn->lpstrInitialDir) tmp.lpstrInitialDir = WDL_UTF8ToWC(lpofn->lpstrInitialDir,0,0,0);
  if (lpofn->lpstrTitle) tmp.lpstrTitle = WDL_UTF8ToWC(lpofn->lpstrTitle,0,0,0);
  if (lpofn->lpstrDefExt) tmp.lpstrDefExt = WDL_UTF8ToWC(lpofn->lpstrDefExt,0,0,0);
  tmp.Flags = lpofn->Flags;
  tmp.lCustData = lpofn->lCustData;
  tmp.lpfnHook = lpofn->lpfnHook;
  tmp.lpTemplateName  = (const WCHAR *)lpofn->lpTemplateName ;
 
  ret=save ? GetSaveFileNameW(&tmp) : GetOpenFileNameW(&tmp);

  // free, convert output
  if (ret)
  {
    if ((tmp.Flags & OFN_ALLOWMULTISELECT) && tmp.lpstrFile[wcslen(tmp.lpstrFile)+1])
    {
      char *op = lpofn->lpstrFile;
      WCHAR *ip = tmp.lpstrFile;
      while (*ip)
      {
        int bcount = WideCharToMultiByte(CP_UTF8,0,ip,-1,NULL,0,NULL,NULL);

        int maxout=lpofn->nMaxFile -2 - (op - lpofn->lpstrFile);
        if (maxout < 2+bcount) break;
        op += WideCharToMultiByte(CP_UTF8,0,ip,-1,op,maxout,NULL,NULL);
        ip += wcslen(ip)+1;
      }
      *op=0;
    }
    else
    {
      int len = WideCharToMultiByte(CP_UTF8,0,tmp.lpstrFile,-1,lpofn->lpstrFile,lpofn->nMaxFile-1,NULL,NULL);
      if (len == 0 && GetLastError()==ERROR_INSUFFICIENT_BUFFER) len = lpofn->nMaxFile-2;
      lpofn->lpstrFile[len]=0;
      if (!len) 
      {
        lpofn->lpstrFile[len+1]=0;
        ret=0;
      }
    }
    // convert 
  }

  lpofn->nFileOffset  = tmp.nFileOffset ;
  lpofn->nFileExtension  = tmp.nFileExtension;
  lpofn->lCustData = tmp.lCustData;

  free((WCHAR *)tmp.lpstrFilter);
  free((WCHAR *)tmp.lpstrFile);
  free((WCHAR *)tmp.lpstrFileTitle);
  free((WCHAR *)tmp.lpstrInitialDir);
  free((WCHAR *)tmp.lpstrTitle);
  free((WCHAR *)tmp.lpstrDefExt );

  lpofn->nFilterIndex  = tmp.nFilterIndex ;
  return ret;
}

BOOL GetOpenFileNameUTF8(LPOPENFILENAME lpofn)
{
  if (GetVersion()< 0x80000000) return GetOpenSaveFileNameUTF8(lpofn,FALSE);
  return GetOpenFileNameA(lpofn);
}

BOOL GetSaveFileNameUTF8(LPOPENFILENAME lpofn)
{
  if (GetVersion()< 0x80000000)return GetOpenSaveFileNameUTF8(lpofn,TRUE);
  return GetSaveFileNameA(lpofn);
}


BOOL SetCurrentDirectoryUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8(path) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok)
    {
      BOOL rv=SetCurrentDirectoryW(wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return SetCurrentDirectoryA(path);
}


HINSTANCE LoadLibraryUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8(path) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok)
    {
      HINSTANCE rv=LoadLibraryW(wbuf);
      if (rv)
      {
        MBTOWIDE_FREE(wbuf);
        return rv;
      }
    }
    MBTOWIDE_FREE(wbuf);
  }
  return LoadLibraryA(path);
}

BOOL CreateDirectoryUTF8(LPCTSTR path, LPSECURITY_ATTRIBUTES attr)
{
  if (WDL_HasUTF8(path) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok)
    {
      BOOL rv=CreateDirectoryW(wbuf,attr);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return CreateDirectoryA(path,attr);
}

BOOL DeleteFileUTF8(LPCTSTR path)
{
  if (WDL_HasUTF8(path) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,path);
    if (wbuf_ok)
    {
      BOOL rv=DeleteFileW(wbuf);
      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return DeleteFileA(path);
}

BOOL MoveFileUTF8(LPCTSTR existfn, LPCTSTR newfn)
{
  if ((WDL_HasUTF8(existfn)||WDL_HasUTF8(newfn)) && GetVersion()< 0x80000000)
  {
    MBTOWIDE(wbuf,existfn);
    if (wbuf_ok)
    {
      MBTOWIDE(wbuf2,newfn);
      if (wbuf2_ok)
      {
        int rv=MoveFileW(wbuf,wbuf2);
        MBTOWIDE_FREE(wbuf2);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }
      MBTOWIDE_FREE(wbuf2);
    }
    MBTOWIDE_FREE(wbuf);
  }
  return MoveFileA(existfn,newfn);
}

DWORD GetCurrentDirectoryUTF8(DWORD nBufferLength, LPTSTR lpBuffer)
{
  if (lpBuffer && nBufferLength > 1 && GetVersion()< 0x80000000)
  {

    WCHAR wbuf[WDL_UTF8_MAXFNLEN];
    wbuf[0]=0;
    GetCurrentDirectoryW(WDL_UTF8_MAXFNLEN,wbuf);
    if (wbuf[0])
    {
      int rv=WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpBuffer,nBufferLength,NULL,NULL);
      if (rv) return rv;
    }
  }
  return GetCurrentDirectoryA(nBufferLength,lpBuffer);
}

HANDLE CreateFileUTF8(LPCTSTR lpFileName,DWORD dwDesiredAccess,DWORD dwShareMode,LPSECURITY_ATTRIBUTES lpSecurityAttributes,DWORD dwCreationDisposition,DWORD dwFlagsAndAttributes,HANDLE hTemplateFile)
{
  if (WDL_HasUTF8(lpFileName) && GetVersion()<0x80000000)
  {
    HANDLE h = INVALID_HANDLE_VALUE;
    
    MBTOWIDE(wstr, lpFileName);
    if (wstr_ok) h = CreateFileW(wstr,dwDesiredAccess,dwShareMode,lpSecurityAttributes,dwCreationDisposition,dwFlagsAndAttributes,hTemplateFile);
    MBTOWIDE_FREE(wstr);

    if (h != INVALID_HANDLE_VALUE) return h;
  }
  return CreateFileA(lpFileName,dwDesiredAccess,dwShareMode,lpSecurityAttributes,dwCreationDisposition,dwFlagsAndAttributes,hTemplateFile);
}


int DrawTextUTF8(HDC hdc, LPCTSTR str, int nc, LPRECT lpRect, UINT format)
{
  if (WDL_HasUTF8(str) && GetVersion()<0x80000000)
  {
    if (nc<0) nc=strlen(str);

    {
      MBTOWIDE(wstr, str);
      if (wstr_ok)
      {
        int rv=DrawTextW(hdc,wstr,-1,lpRect,format);;
        MBTOWIDE_FREE(wstr);
        return rv;
      }
      MBTOWIDE_FREE(wstr);
    }

  }
  return DrawTextA(hdc,str,nc,lpRect,format);
}


BOOL InsertMenuItemUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (lpmii && (lpmii->fMask & MIIM_TYPE) && lpmii->fType == MFT_STRING && lpmii->dwTypeData && WDL_HasUTF8(lpmii->dwTypeData) && GetVersion()<0x80000000)
  {
    BOOL rv;
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    MBTOWIDE(wbuf,lpmii->dwTypeData);
    if (wbuf_ok)
    {

      tmp.cbSize=sizeof(tmp);
      tmp.dwTypeData = wbuf;
      rv=InsertMenuItemW(hMenu,uItem,fByPosition,&tmp);

      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return InsertMenuItemA(hMenu,uItem,fByPosition,lpmii);
}
BOOL SetMenuItemInfoUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (lpmii && (lpmii->fMask & MIIM_TYPE) && lpmii->fType == MFT_STRING && lpmii->dwTypeData && WDL_HasUTF8(lpmii->dwTypeData) && GetVersion()<0x80000000)
  {
    BOOL rv;
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    MBTOWIDE(wbuf,lpmii->dwTypeData);
    if (wbuf_ok)
    {
      tmp.cbSize=sizeof(tmp);
      tmp.dwTypeData = wbuf;
      rv=SetMenuItemInfoW(hMenu,uItem,fByPosition,&tmp);

      MBTOWIDE_FREE(wbuf);
      return rv;
    }
    MBTOWIDE_FREE(wbuf);
  }
  return SetMenuItemInfoA(hMenu,uItem,fByPosition,lpmii);
}

BOOL GetMenuItemInfoUTF8( HMENU hMenu,UINT uItem, BOOL fByPosition, LPMENUITEMINFO lpmii)
{
  if (lpmii && (lpmii->fMask & MIIM_TYPE) && lpmii->dwTypeData && lpmii->cch && GetVersion()<0x80000000)
  {
    BOOL rv;
    MENUITEMINFOW tmp = *(MENUITEMINFOW*)lpmii;
    WIDETOMB_ALLOC(wbuf,lpmii->cch);

    char *otd=lpmii->dwTypeData;
    int osz=lpmii->cbSize;
    tmp.cbSize=sizeof(tmp);
    tmp.dwTypeData = wbuf;
    rv=GetMenuItemInfoW(hMenu,uItem,fByPosition,&tmp);

    if (rv && tmp.fType == MFT_STRING)
    {
      if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,lpmii->dwTypeData,lpmii->cch,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
      {
        lpmii->dwTypeData[lpmii->cch-1]=0;
      }

      *lpmii = *(MENUITEMINFO*)&tmp; // copy results
      lpmii->cbSize=osz; // restore old stuff
      lpmii->dwTypeData = otd;
    }
    else rv=0;

    WIDETOMB_FREE(wbuf);
    if (rv)return rv;
  }
  return GetMenuItemInfoA(hMenu,uItem,fByPosition,lpmii);
}

int statUTF8(const char *filename, struct stat *buffer)
{
  if (WDL_HasUTF8(filename) && GetVersion()<0x80000000)
  {
    MBTOWIDE(wbuf,filename);
    if (wbuf_ok)
    {
      int rv=_wstat(wbuf,(struct _stat*)buffer);
      MBTOWIDE_FREE(wbuf);
      if (!rv) return rv;
    }
    else
    {
      MBTOWIDE_FREE(wbuf);
    }
  }
  return stat(filename,buffer);
}

LPSTR GetCommandParametersUTF8()
{
  char *buf;
  int szneeded;
  LPWSTR w=GetCommandLineW();
  if (!w) return NULL;
  szneeded = WideCharToMultiByte(CP_UTF8,0,w,-1,NULL,0,NULL,NULL);
  if (szneeded<1) return NULL;
  buf = (char *)malloc(szneeded+10);
  if (!buf) return NULL;
  if (WideCharToMultiByte(CP_UTF8,0,w,-1,buf,szneeded+9,NULL,NULL)<1) return NULL;
  while (*buf == ' ') buf++;
  if (*buf == '\"')
  {
    buf++;
    while (*buf && *buf != '\"') buf++;
  }
  else
  {
    while (*buf && *buf != ' ') buf++;
  }
  if (*buf) buf++;
  while (*buf == ' ') buf++;
  if (*buf) return buf;

  return NULL;
}


#if defined(WDL_WIN32_UTF8_IMPL_NOTSTATIC) || defined(WDL_WIN32_UTF8_IMPL_STATICHOOKS)


#define WDL_UTF8_OLDPROCPROP "WDLUTF8OldProc"

static LRESULT WINAPI cb_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC oldproc = (WNDPROC)GetProp(hwnd,WDL_UTF8_OLDPROCPROP);
  if (!oldproc) return 0;

  if (msg==WM_NCDESTROY)
  {
    SetWindowLongPtr(hwnd, GWLP_WNDPROC,(INT_PTR)oldproc);
    RemoveProp(hwnd,WDL_UTF8_OLDPROCPROP);
  }
  else if (msg == CB_ADDSTRING || msg == CB_INSERTSTRING || msg == LB_ADDSTRING || msg == LB_INSERTSTRING)
  {
    char *str=(char*)lParam;
    if (lParam && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv=CallWindowProcW(oldproc,hwnd,msg,wParam,(LPARAM)wbuf);
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }
  // todo: hook string getting too?

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}

void WDL_UTF8_HookComboBox(HWND h)
{
  if (!h||GetVersion()>=0x80000000||GetProp(h,WDL_UTF8_OLDPROCPROP)) return;
  SetProp(h,WDL_UTF8_OLDPROCPROP,(HANDLE)SetWindowLongPtr(h,GWLP_WNDPROC,(INT_PTR)cb_newProc));
}

void WDL_UTF8_HookListBox(HWND h)
{
  WDL_UTF8_HookComboBox(h);
}



static LRESULT WINAPI lv_newProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC oldproc = (WNDPROC)GetProp(hwnd,WDL_UTF8_OLDPROCPROP);
  if (!oldproc) return 0;

  if (msg==WM_NCDESTROY)
  {
    SetWindowLongPtr(hwnd, GWLP_WNDPROC,(INT_PTR)oldproc);
    RemoveProp(hwnd,WDL_UTF8_OLDPROCPROP);
  }
  else if (msg == LVM_INSERTITEMA || msg == LVM_SETITEMA || msg == LVM_SETITEMTEXTA) 
  {
    LPLVITEM pItem = (LPLVITEM) lParam;
    char *str;
    if (pItem && (str=pItem->pszText) && (msg==LVM_SETITEMTEXTA || (pItem->mask&LVIF_TEXT)) && WDL_HasUTF8(str))
    {
      MBTOWIDE(wbuf,str);
      if (wbuf_ok)
      {
        LRESULT rv;
        pItem->pszText=(char*)wbuf; // set new buffer
        rv=CallWindowProc(oldproc,hwnd,msg == LVM_INSERTITEMA ? LVM_INSERTITEMW : msg == LVM_SETITEMA ? LVM_SETITEMW : LVM_SETITEMTEXTW,wParam,lParam);
        pItem->pszText = str; // restore old pointer
        MBTOWIDE_FREE(wbuf);
        return rv;
      }

      MBTOWIDE_FREE(wbuf);
    }
  }
  else if (msg==LVM_GETITEMA||msg==LVM_GETITEMTEXTA)
  {
    LPLVITEM pItem = (LPLVITEM) lParam;
    char *obuf;
    if (pItem && (msg == LVM_GETITEMTEXTA || (pItem->mask & LVIF_TEXT)) && (obuf=pItem->pszText) && pItem->cchTextMax > 3)
    {
      WIDETOMB_ALLOC(wbuf,pItem->cchTextMax);
      *obuf=0;
      if (wbuf)
      {
        LRESULT rv;
        int oldsz=pItem->cchTextMax;
        *wbuf=0;
        pItem->cchTextMax=wbuf_size/sizeof(WCHAR);
        pItem->pszText = (char *)wbuf;
        rv=CallWindowProc(oldproc,hwnd,msg==LVM_GETITEMTEXTA ? LVM_GETITEMTEXTW : LVM_GETITEMW,wParam,lParam);

        if (!WideCharToMultiByte(CP_UTF8,0,wbuf,-1,obuf,oldsz,NULL,NULL) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
          obuf[oldsz-1]=0;

        pItem->cchTextMax=oldsz;
        pItem->pszText=obuf;
        WIDETOMB_FREE(wbuf);

        if (obuf[0]) return rv;
      }
      else
      {
        WIDETOMB_FREE(wbuf);
      }

    }
  }

  return CallWindowProc(oldproc,hwnd,msg,wParam,lParam);
}

void WDL_UTF8_HookListView(HWND h)
{
  if (!h||GetVersion()>=0x80000000||GetProp(h,WDL_UTF8_OLDPROCPROP)) return;
  SetProp(h,WDL_UTF8_OLDPROCPROP,(HANDLE)SetWindowLongPtr(h,GWLP_WNDPROC,(INT_PTR)lv_newProc));
}

void WDL_UTF8_ListViewConvertDispInfoToW(void *_di)
{
  NMLVDISPINFO *di = (NMLVDISPINFO *)_di;
  if (di && (di->item.mask & LVIF_TEXT) && di->item.pszText && di->item.cchTextMax>0)
  {
    char tmp_buf[1024], *tmp=tmp_buf;
    char *src = di->item.pszText;

    if (strlen(src) < 1024) strcpy(tmp,src);
    else tmp = strdup(src);

    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,tmp,-1,(LPWSTR)di->item.pszText,di->item.cchTextMax))
    {
      if (GetLastError()==ERROR_INSUFFICIENT_BUFFER)
      {
        ((WCHAR *)di->item.pszText)[di->item.cchTextMax-1] = 0;
      }
      else
      {
        if (!MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,tmp,-1,(LPWSTR)di->item.pszText,di->item.cchTextMax) && GetLastError()==ERROR_INSUFFICIENT_BUFFER)
          ((WCHAR *)di->item.pszText)[di->item.cchTextMax-1] = 0;
      }
    }   

    if (tmp!=tmp_buf) free(tmp);

  }
}

#endif

#ifdef __cplusplus
};
#endif

#endif

#endif //_WIN32
