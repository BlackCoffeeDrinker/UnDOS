#ifndef UNDOS_WINUSER_H
#define UNDOS_WINUSER_H

/* Standard controls, scroll bars and the dialog manager. Included by
   windows.h; do not include directly. */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef intptr_t INT_PTR;
typedef uint16_t WCHAR;
typedef INT_PTR (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);

#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_NEXTDLGCTL 0x0028
#define WM_DRAWITEM 0x002B
#define WM_MEASUREITEM 0x002C
#define WM_DELETEITEM 0x002D
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_GETDLGCODE 0x0087
#define WM_INITDIALOG 0x0110
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define DM_GETDEFID (WM_USER + 0)
#define DM_SETDEFID (WM_USER + 1)
#define DC_HASDEFID 0x534B

#define WS_GROUP 0x00020000u
#define WS_TABSTOP 0x00010000u
#define WS_VSCROLL 0x00200000u
#define WS_HSCROLL 0x00100000u
#define WS_DLGFRAME 0x00400000u
#define ERROR_CONTROL_ID_NOT_FOUND 1421
#define GWL_ID (-12)
#define GWLP_ID GWL_ID
#define DWLP_MSGRESULT 0
#define DWLP_DLGPROC 4
#define DWLP_USER 8
#define DWL_MSGRESULT DWLP_MSGRESULT
#define DWL_DLGPROC DWLP_DLGPROC
#define DWL_USER DWLP_USER

#define GW_HWNDFIRST 0
#define GW_HWNDLAST 1
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5

#define COLOR_SCROLLBAR 0
#define COLOR_WINDOW 5
#define COLOR_WINDOWFRAME 6
#define COLOR_WINDOWTEXT 8
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_BTNFACE 15
#define COLOR_BTNSHADOW 16
#define COLOR_GRAYTEXT 17
#define COLOR_BTNTEXT 18
#define COLOR_BTNHIGHLIGHT 20

/* Buttons */
#define BS_PUSHBUTTON 0x0000u
#define BS_DEFPUSHBUTTON 0x0001u
#define BS_CHECKBOX 0x0002u
#define BS_AUTOCHECKBOX 0x0003u
#define BS_RADIOBUTTON 0x0004u
#define BS_3STATE 0x0005u
#define BS_AUTO3STATE 0x0006u
#define BS_GROUPBOX 0x0007u
#define BS_AUTORADIOBUTTON 0x0009u
#define BS_OWNERDRAW 0x000Bu
#define BS_TYPEMASK 0x000Fu
#define BS_LEFTTEXT 0x0020u
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BM_GETSTATE 0x00F2
#define BM_SETSTATE 0x00F3
#define BM_SETSTYLE 0x00F4
#define BM_CLICK 0x00F5
#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001
#define BST_INDETERMINATE 0x0002
#define BST_PUSHED 0x0004
#define BST_FOCUS 0x0008
#define BN_CLICKED 0
#define BN_DOUBLECLICKED 5

/* Static */
#define SS_LEFT 0x0000u
#define SS_ICON 0x0003u
#define SS_CENTER 0x0001u
#define SS_RIGHT 0x0002u
#define SS_BLACKRECT 0x0004u
#define SS_GRAYRECT 0x0005u
#define SS_WHITERECT 0x0006u
#define SS_BLACKFRAME 0x0007u
#define SS_GRAYFRAME 0x0008u
#define SS_WHITEFRAME 0x0009u
#define SS_SIMPLE 0x000Bu
#define SS_LEFTNOWORDWRAP 0x000Cu
#define SS_TYPEMASK 0x001Fu
#define SS_NOPREFIX 0x0080u

/* Edit (single line) */
#define ES_LEFT 0x0000u
#define ES_MULTILINE 0x0004u
#define ES_CENTER 0x0001u
#define ES_RIGHT 0x0002u
#define ES_UPPERCASE 0x0008u
#define ES_LOWERCASE 0x0010u
#define ES_PASSWORD 0x0020u
#define ES_AUTOHSCROLL 0x0080u
#define ES_READONLY 0x0800u
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_GETMODIFY 0x00B8
#define EM_SETMODIFY 0x00B9
#define EM_REPLACESEL 0x00C2
#define EM_LIMITTEXT 0x00C5
#define EM_SETLIMITTEXT EM_LIMITTEXT
#define EM_SETREADONLY 0x00CF
#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400
#define EN_MAXTEXT 0x0501

/* List box */
#define LBS_NOTIFY 0x0001u
#define LBS_SORT 0x0002u
#define LBS_NOREDRAW 0x0004u
#define LBS_MULTIPLESEL 0x0008u
#define LBS_OWNERDRAWFIXED 0x0010u
#define LBS_OWNERDRAWVARIABLE 0x0020u
#define LBS_HASSTRINGS 0x0040u
#define LBS_USETABSTOPS 0x0080u
#define LBS_NOINTEGRALHEIGHT 0x0100u
#define LBS_WANTKEYBOARDINPUT 0x0400u
#define LBS_EXTENDEDSEL 0x0800u
#define LBS_STANDARD 0x00A00003u
#define LBS_DISABLENOSCROLL 0x1000u
/* Combo box styles (Win32 values); the combo box class is not implemented yet. */
#define CBS_SIMPLE 0x0001u
#define CBS_DROPDOWN 0x0002u
#define CBS_DROPDOWNLIST 0x0003u
#define CBS_OWNERDRAWFIXED 0x0010u
#define CBS_OWNERDRAWVARIABLE 0x0020u
#define CBS_AUTOHSCROLL 0x0040u
#define CBS_OEMCONVERT 0x0080u
#define CBS_SORT 0x0100u
#define CBS_HASSTRINGS 0x0200u
#define CBS_NOINTEGRALHEIGHT 0x0400u
#define CBS_DISABLENOSCROLL 0x0800u
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETSEL 0x0185
#define LB_SETCURSEL 0x0186
#define LB_GETSEL 0x0187
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018A
#define LB_GETCOUNT 0x018B
#define LB_SELECTSTRING 0x018C
#define LB_GETTOPINDEX 0x018E
#define LB_FINDSTRING 0x018F
#define LB_GETSELCOUNT 0x0190
#define LB_GETSELITEMS 0x0191
#define LB_SETTOPINDEX 0x0197
#define LB_GETITEMRECT 0x0198
#define LB_GETITEMDATA 0x0199
#define LB_SETITEMDATA 0x019A
#define LB_SETCARETINDEX 0x019E
#define LB_GETCARETINDEX 0x019F
#define LB_SETITEMHEIGHT 0x01A0
#define LB_GETITEMHEIGHT 0x01A1
#define LB_FINDSTRINGEXACT 0x01A2
#define LB_OKAY 0
#define LB_ERR (-1)
#define LB_ERRSPACE (-2)
#define LBN_ERRSPACE (-2)
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2
#define LBN_SELCANCEL 3
#define LBN_SETFOCUS 4
#define LBN_KILLFOCUS 5

/* Owner draw */
#define ODT_LISTBOX 2
#define ODT_BUTTON 4
#define ODA_DRAWENTIRE 0x0001u
#define ODA_SELECT 0x0002u
#define ODA_FOCUS 0x0004u
#define ODS_SELECTED 0x0001u
#define ODS_GRAYED 0x0002u
#define ODS_DISABLED 0x0004u
#define ODS_CHECKED 0x0008u
#define ODS_FOCUS 0x0010u

typedef struct tagDRAWITEMSTRUCT {
  UINT CtlType;
  UINT CtlID;
  UINT itemID;
  UINT itemAction;
  UINT itemState;
  HWND hwndItem;
  HDC hDC;
  RECT rcItem;
  ULONG_PTR itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef struct tagMEASUREITEMSTRUCT {
  UINT CtlType;
  UINT CtlID;
  UINT itemID;
  UINT itemWidth;
  UINT itemHeight;
  ULONG_PTR itemData;
} MEASUREITEMSTRUCT, *LPMEASUREITEMSTRUCT;

typedef struct tagDELETEITEMSTRUCT {
  UINT CtlType;
  UINT CtlID;
  UINT itemID;
  HWND hwndItem;
  ULONG_PTR itemData;
} DELETEITEMSTRUCT, *LPDELETEITEMSTRUCT;

/* Scroll bars */
#define SBS_HORZ 0x0000u
#define SBS_VERT 0x0001u
#define SB_HORZ 0
#define SB_VERT 1
#define SB_CTL 2
#define SB_LINEUP 0
#define SB_LINELEFT 0
#define SB_LINEDOWN 1
#define SB_LINERIGHT 1
#define SB_PAGEUP 2
#define SB_PAGELEFT 2
#define SB_PAGEDOWN 3
#define SB_PAGERIGHT 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_LEFT 6
#define SB_BOTTOM 7
#define SB_RIGHT 7
#define SB_ENDSCROLL 8
#define SIF_RANGE 0x0001u
#define SIF_PAGE 0x0002u
#define SIF_POS 0x0004u
#define SIF_DISABLENOSCROLL 0x0008u
#define SIF_TRACKPOS 0x0010u
#define SIF_ALL (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)
#define SBM_SETPOS 0x00E0
#define SBM_GETPOS 0x00E1
#define SBM_SETRANGE 0x00E2
#define SBM_GETRANGE 0x00E3
#define SBM_SETSCROLLINFO 0x00E9
#define SBM_GETSCROLLINFO 0x00EA

typedef struct tagSCROLLINFO {
  UINT cbSize;
  UINT fMask;
  int nMin;
  int nMax;
  UINT nPage;
  int nPos;
  int nTrackPos;
} SCROLLINFO, *LPSCROLLINFO;
typedef const SCROLLINFO *LPCSCROLLINFO;

/* Dialogs */
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#define DS_SETFONT 0x0040u
#define DS_MODALFRAME 0x0080u
#define DS_SYSMODAL 0x0002u
#define DS_CENTER 0x0800u
#define DLGC_WANTARROWS 0x0001
#define DLGC_WANTTAB 0x0002
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_HASSETSEL 0x0008
#define DLGC_DEFPUSHBUTTON 0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_RADIOBUTTON 0x0040
#define DLGC_WANTCHARS 0x0080
#define DLGC_STATIC 0x0100
#define DLGC_BUTTON 0x2000
#define RT_DIALOG MAKEINTRESOURCE(5)

/* Binary layout produced by the resource compiler: WORD-packed, strings
   and class names follow as UTF-16 sz_Or_Ord fields. */
#pragma pack(push, 2)
typedef struct {
  DWORD style;
  DWORD dwExtendedStyle;
  WORD cdit;
  short x;
  short y;
  short cx;
  short cy;
} DLGTEMPLATE;
typedef struct {
  DWORD style;
  DWORD dwExtendedStyle;
  short x;
  short y;
  short cx;
  short cy;
  WORD id;
} DLGITEMTEMPLATE;
#pragma pack(pop)
typedef const DLGTEMPLATE *LPCDLGTEMPLATE;

#define WC_BUTTON "Button"
#define WC_EDIT "Edit"
#define WC_STATIC "Static"
#define WC_LISTBOX "ListBox"
#define WC_SCROLLBAR "ScrollBar"
#define WC_DIALOG "#32770"

/* Windows */
HWND GetParent(HWND window);
HWND GetWindow(HWND window, UINT command);
BOOL IsWindow(HWND window);
BOOL IsChild(HWND parent, HWND window);
BOOL EnableWindow(HWND window, BOOL enable);
BOOL IsWindowEnabled(HWND window);
HWND SetFocus(HWND window);
HWND GetFocus(void);
int GetWindowText(HWND window, LPSTR buffer, int size);
int GetWindowTextLength(HWND window);
int GetDlgCtrlID(HWND window);
DWORD GetSysColor(int index);
HBRUSH GetSysColorBrush(int index);
BOOL DrawFocusRect(HDC dc, const RECT *rect);

/* Scroll bars */
int SetScrollPos(HWND window, int bar, int position, BOOL redraw);
int GetScrollPos(HWND window, int bar);
BOOL SetScrollRange(HWND window, int bar, int minimum, int maximum,
                    BOOL redraw);
BOOL GetScrollRange(HWND window, int bar, int *minimum, int *maximum);
int SetScrollInfo(HWND window, int bar, LPCSCROLLINFO info, BOOL redraw);
BOOL GetScrollInfo(HWND window, int bar, LPSCROLLINFO info);

/* Dialog manager */
INT_PTR DialogBoxIndirectParam(HINSTANCE instance, LPCDLGTEMPLATE dialog,
                               HWND owner, DLGPROC procedure, LPARAM param);
INT_PTR DialogBoxParam(HINSTANCE instance, LPCSTR name, HWND owner,
                       DLGPROC procedure, LPARAM param);
HWND CreateDialogIndirectParam(HINSTANCE instance, LPCDLGTEMPLATE dialog,
                               HWND owner, DLGPROC procedure, LPARAM param);
HWND CreateDialogParam(HINSTANCE instance, LPCSTR name, HWND owner,
                       DLGPROC procedure, LPARAM param);
BOOL EndDialog(HWND dialog, INT_PTR result);
LRESULT DefDlgProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
BOOL IsDialogMessage(HWND dialog, MSG *message);
HWND GetDlgItem(HWND dialog, int id);
UINT GetDlgItemText(HWND dialog, int id, LPSTR buffer, int size);
BOOL SetDlgItemText(HWND dialog, int id, LPCSTR text);
UINT GetDlgItemInt(HWND dialog, int id, BOOL *translated, BOOL isSigned);
BOOL SetDlgItemInt(HWND dialog, int id, UINT value, BOOL isSigned);
LRESULT SendDlgItemMessage(HWND dialog, int id, UINT message, WPARAM wParam,
                           LPARAM lParam);
BOOL CheckDlgButton(HWND dialog, int id, UINT check);
UINT IsDlgButtonChecked(HWND dialog, int id);
BOOL CheckRadioButton(HWND dialog, int first, int last, int check);
HWND GetNextDlgTabItem(HWND dialog, HWND control, BOOL previous);
HWND GetNextDlgGroupItem(HWND dialog, HWND control, BOOL previous);
LONG GetDialogBaseUnits(void);
BOOL MapDialogRect(HWND dialog, RECT *rect);

/* Rectangles and coordinates */
BOOL CopyRect(RECT *destination, const RECT *source);
BOOL SetRectEmpty(RECT *rect);
BOOL IsRectEmpty(const RECT *rect);
BOOL EqualRect(const RECT *a, const RECT *b);
BOOL PtInRect(const RECT *rect, POINT point);
BOOL IntersectRect(RECT *destination, const RECT *a, const RECT *b);
BOOL UnionRect(RECT *destination, const RECT *a, const RECT *b);
int FrameRect(HDC dc, const RECT *rect, HBRUSH brush);
BOOL ClientToScreen(HWND window, POINT *point);
BOOL ScreenToClient(HWND window, POINT *point);
int MapWindowPoints(HWND from, HWND to, POINT *points, UINT count);

/* Window placement. SetWindowPos ignores Z-order (insertAfter). */
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOOWNERZORDER 0x0200
BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width,
                  int height, UINT flags);
BOOL UpdateWindow(HWND window);
BOOL IsIconic(HWND window);
/* Window DCs outside BeginPaint need kernel support; GetDC fails with
   ERROR_CALL_NOT_IMPLEMENTED for now. */
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);

/* Message boxes */
#define MB_OK 0x00000000u
#define MB_OKCANCEL 0x00000001u
#define MB_ABORTRETRYIGNORE 0x00000002u
#define MB_YESNOCANCEL 0x00000003u
#define MB_YESNO 0x00000004u
#define MB_RETRYCANCEL 0x00000005u
#define MB_ICONHAND 0x00000010u
#define MB_ICONQUESTION 0x00000020u
#define MB_ICONEXCLAMATION 0x00000030u
#define MB_ICONASTERISK 0x00000040u
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONSTOP MB_ICONHAND
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_DEFBUTTON1 0x00000000u
#define MB_DEFBUTTON2 0x00000100u
#define MB_DEFBUTTON3 0x00000200u
#define MB_APPLMODAL 0x00000000u
#define MB_SYSTEMMODAL 0x00001000u
#define MB_TASKMODAL 0x00002000u
#define MB_SETFOREGROUND 0x00010000u
#define MB_TYPEMASK 0x0000000Fu
#define MB_ICONMASK 0x000000F0u
#define MB_DEFMASK 0x00000F00u
/* Icons are not drawn. Returns the ID of the button pressed, or 0. */
int MessageBox(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
BOOL MessageBeep(UINT type);

/* Menus: removal */
BOOL RemoveMenu(HMENU menu, UINT item, UINT flags);
BOOL DeleteMenu(HMENU menu, UINT item, UINT flags);
BOOL DrawMenuBar(HWND window);

/* Input: timers, capture, cursors, drag detection */
#define WM_TIMER 0x0113
#define WM_SETCURSOR 0x0020
#define WM_CHILDACTIVATE 0x0022
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSELAST 0x0209
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define USER_TIMER_MINIMUM 0x0000000A
#define USER_TIMER_MAXIMUM 0x7FFFFFFF

typedef void(CALLBACK *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC procedure);
BOOL KillTimer(HWND window, UINT_PTR id);
DWORD GetTickCount(void);
UINT GetDoubleClickTime(void);

HWND SetCapture(HWND window);
BOOL ReleaseCapture(void);
HWND GetCapture(void);
BOOL DragDetect(HWND window, POINT point);

#define RT_CURSOR MAKEINTRESOURCE(1)
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define IDC_IBEAM MAKEINTRESOURCE(32513)
#define IDC_WAIT MAKEINTRESOURCE(32514)
#define IDC_CROSS MAKEINTRESOURCE(32515)
#define IDC_UPARROW MAKEINTRESOURCE(32516)
#define IDC_SIZE MAKEINTRESOURCE(32640)
#define IDC_ICON MAKEINTRESOURCE(32641)
#define IDC_SIZENWSE MAKEINTRESOURCE(32642)
#define IDC_SIZENESW MAKEINTRESOURCE(32643)
#define IDC_SIZEWE MAKEINTRESOURCE(32644)
#define IDC_SIZENS MAKEINTRESOURCE(32645)
#define IDC_SIZEALL MAKEINTRESOURCE(32646)
#define IDC_NO MAKEINTRESOURCE(32648)
#define IDC_HAND MAKEINTRESOURCE(32649)
#define IDC_APPSTARTING MAKEINTRESOURCE(32650)
HCURSOR LoadCursor(HINSTANCE instance, LPCSTR name);
HCURSOR SetCursor(HCURSOR cursor);
HCURSOR GetCursor(void);
int ShowCursor(BOOL show);

#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CYCAPTION 4
#define SM_CXBORDER 5
#define SM_CYBORDER 6
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CYMENU 15
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXDRAG 68
#define SM_CYDRAG 69
int GetSystemMetrics(int index);

/* MDI */
#define WC_MDICLIENT "MDIClient"
#ifndef SC_NEXTWINDOW
#define SC_NEXTWINDOW 0xF040u
#define SC_PREVWINDOW 0xF050u
#endif
#define WM_MDICREATE 0x0220
#define WM_MDIDESTROY 0x0221
#define WM_MDIACTIVATE 0x0222
#define WM_MDIRESTORE 0x0223
#define WM_MDINEXT 0x0224
#define WM_MDIMAXIMIZE 0x0225
#define WM_MDITILE 0x0226
#define WM_MDICASCADE 0x0227
#define WM_MDIICONARRANGE 0x0228
#define WM_MDIGETACTIVE 0x0229
#define WM_MDISETMENU 0x0230
#define WM_MDIREFRESHMENU 0x0234
#define MDITILE_VERTICAL 0x0000
#define MDITILE_HORIZONTAL 0x0001
#define MDITILE_SKIPDISABLED 0x0002
#define MDIS_ALLCHILDSTYLES 0x0001
#define WS_EX_MDICHILD 0x00000040L
#define UNDOS_MDI_MENU_WINDOWS 9

typedef struct tagCLIENTCREATESTRUCT {
  HMENU hWindowMenu;
  UINT idFirstChild;
} CLIENTCREATESTRUCT, *LPCLIENTCREATESTRUCT;

typedef struct tagMDICREATESTRUCT {
  LPCSTR szClass;
  LPCSTR szTitle;
  HINSTANCE hOwner;
  int x;
  int y;
  int cx;
  int cy;
  DWORD style;
  LPARAM lParam;
} MDICREATESTRUCT, *LPMDICREATESTRUCT;

LRESULT DefFrameProc(HWND frame, HWND client, UINT message, WPARAM wParam,
                     LPARAM lParam);
LRESULT DefMDIChildProc(HWND child, UINT message, WPARAM wParam,
                        LPARAM lParam);
BOOL TranslateMDISysAccel(HWND client, MSG *message);
HWND CreateMDIWindow(LPCSTR className, LPCSTR title, DWORD style, int x,
                     int y, int width, int height, HWND client,
                     HINSTANCE instance, LPARAM lParam);

#define DialogBox(instance, name, owner, procedure) \
  DialogBoxParam(instance, name, owner, procedure, 0)
#define DialogBoxIndirect(instance, dialog, owner, procedure) \
  DialogBoxIndirectParam(instance, dialog, owner, procedure, 0)
#define CreateDialog(instance, name, owner, procedure) \
  CreateDialogParam(instance, name, owner, procedure, 0)
#define CreateDialogIndirect(instance, dialog, owner, procedure) \
  CreateDialogIndirectParam(instance, dialog, owner, procedure, 0)

#ifdef __cplusplus
}
#endif

#endif
