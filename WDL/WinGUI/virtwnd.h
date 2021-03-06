/*
    WDL - virtwnd.h
    Copyright (C) 2006 and later Cockos Incorporated

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
      

    This file provides interfaces for the WDL Virtual Windows layer, a system that allows
    creating many controls within one system device context.

    The base class is a WDL_VWnd.

    If you create a WDL_VWnd, you should send it (or its parent) mouse messages etc.

    To paint a WDL_VWnd, use a WDL_VWnd_Painter in WM_PAINT etc.


    More documentation should follow...
*/



#ifndef _WDL_VIRTWND_H_
#define _WDL_VIRTWND_H_

#ifdef _WIN32
#include <windows.h>
#else
#include "../swell/swell.h"
#endif
#include "../ptrlist.h"
#include "../wdlstring.h"



class LICE_IBitmap;

// deprecated
#define WDL_VirtualWnd_ChildList WDL_VWnd
#define WDL_VirtualWnd WDL_VWnd
#define WDL_VirtualWnd_Painter WDL_VWnd_Painter
class WDL_VWnd
{
public:
  WDL_VWnd();
  virtual ~WDL_VWnd();
  virtual void SetID(int id) { m_id=id; }
  virtual int GetID() { return m_id; }
  virtual INT_PTR GetUserData() { return m_userdata; }
  virtual INT_PTR SetUserData(INT_PTR ud) { INT_PTR od=m_userdata; m_userdata=ud; return od; }
  virtual void SetPosition(const RECT *r) { m_position=*r; }
  virtual void GetPosition(RECT *r) { *r=m_position; }
  virtual void GetPositionPaintExtent(RECT *r) { *r=m_position; }
  virtual void GetPositionPaintOverExtent(RECT *r) { *r=m_position; }
  virtual void SetVisible(bool vis) { m_visible=vis; }
  virtual bool IsVisible() { return m_visible; }
  virtual bool WantsPaintOver() { return m_children && m_children->GetSize(); }
  virtual WDL_VWnd *GetParent() { return m_parent; }
  virtual void SetParent(WDL_VWnd *par) { m_parent=par; }

  virtual void RequestRedraw(RECT *r); 
  virtual void OnPaint(LICE_IBitmap *drawbm, int origin_x, int origin_y, RECT *cliprect);
  virtual void OnPaintOver(LICE_IBitmap *drawbm, int origin_x, int origin_y, RECT *cliprect);

  virtual int OnMouseDown(int xpos, int ypos); // return -1 to eat, >0 to capture
  virtual bool OnMouseDblClick(int xpos, int ypos);
  virtual bool OnMouseWheel(int xpos, int ypos, int amt);

  virtual void OnMouseMove(int xpos, int ypos);
  virtual void OnMouseUp(int xpos, int ypos);

  // child windows
  virtual WDL_VWnd *EnumChildren(int x);
  virtual int GetNumChildren();
  virtual WDL_VWnd *GetChildByID(int id);
  virtual void AddChild(WDL_VWnd *wnd, int pos=-1);
  virtual void RemoveChild(WDL_VWnd *wnd, bool dodel=false);
  virtual void RemoveAllChildren(bool dodel=true);
  virtual WDL_VWnd *GetCaptureWnd() { return m_children ? m_children->Get(m_captureidx) : 0; }
  virtual WDL_VWnd *VirtWndFromPoint(int xpos, int ypos, int maxdepth=-1); // maxdepth=0 only direct children, etc, -1 is unlimited

  // OS access
  virtual HWND GetRealParent() { if (m_realparent) return m_realparent; if (GetParent()) return GetParent()->GetRealParent(); return 0; }
  virtual void SetRealParent(HWND par) { m_realparent=par; }

  virtual INT_PTR SendCommand(int command, INT_PTR parm1, INT_PTR parm2, WDL_VWnd *src);

  // request if window has cursor
  virtual int UpdateCursor(int xpos, int ypos); // >0 if set, 0 if cursor wasnt set , <0 if cursor should be default...
  virtual bool GetToolTipString(int xpos, int ypos, char *bufOut, int bufOutSz); // true if handled

protected:
  WDL_VWnd *m_parent;
  bool m_visible;
  int m_id;
  RECT m_position;
  INT_PTR m_userdata;

  HWND m_realparent;
  int m_captureidx;
  int m_lastmouseidx;
  WDL_PtrList<WDL_VWnd> *m_children;

};


// painting object (can be per window or per thread or however you like)
#define WDL_VWP_SUNKENBORDER 0x00010000
#define WDL_VWP_SUNKENBORDER_NOTOP 0x00020000
#define WDL_VWP_DIVIDER_VERT 0x00030000
#define WDL_VWP_DIVIDER_HORZ 0x00040000


#include "virtwnd-skin.h"

class WDL_VWnd_Painter
{
public:
  WDL_VWnd_Painter();
  ~WDL_VWnd_Painter();


  void SetGSC(int (*GSC)(int));
  void PaintBegin(HWND hwnd, int bgcolor=-1);  
  void SetBGImage(WDL_VirtualWnd_BGCfg *bitmap, int tint=-1) { m_bgbm=bitmap; m_bgbmtintcolor=tint; } // call before every paintbegin (resets if you dont)
  void SetBGGradient(int wantGradient, double start, double slope); // wantg < 0 to use system defaults

  void PaintVirtWnd(WDL_VWnd *vwnd, int borderflags=0);
  void PaintBorderForHWND(HWND hwnd, int borderflags);
  void PaintBorderForRect(const RECT *r, int borderflags);

  void GetPaintInfo(RECT *rclip, int *xoffsdraw, int *yoffsdraw);

  LICE_IBitmap *GetBuffer(int *xo, int *yo) 
  { 
    *xo = -m_paint_xorig;
    *yo = -m_paint_yorig;
    return m_bm; 
  }

  void PaintEnd();

private:

  double m_gradstart,m_gradslope;

  int m_wantg;
  int (*m_GSC)(int);
  void DoPaintBackground(int bgcolor, RECT *clipr, int wnd_w, int wnd_h);
  LICE_IBitmap *m_bm;
  WDL_VirtualWnd_BGCfg *m_bgbm;
  int m_bgbmtintcolor;

  HWND m_cur_hwnd;
  PAINTSTRUCT m_ps;
  int m_paint_xorig, m_paint_yorig;

};

void WDL_VWnd_regHelperClass(const char *classname, void *icon=NULL, void *iconsm=NULL); // register this class if you wish to make your dialogs use it (better paint behavior)

#endif