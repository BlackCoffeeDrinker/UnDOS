/* UnDOS: commdlg.h shim. UnDOS has no comdlg32; WINFILE loads it with
   LoadLibrary, which fails, so the Font and Browse commands back out. Only
   the types and flags it fills in are declared here. */
#pragma once
#include <windows.h>

typedef UINT_PTR (CALLBACK *LPCFHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagCHOOSEFONT {
  DWORD lStructSize;
  HWND hwndOwner;
  HDC hDC;
  LPLOGFONT lpLogFont;
  INT iPointSize;
  DWORD Flags;
  COLORREF rgbColors;
  LPARAM lCustData;
  LPCFHOOKPROC lpfnHook;
  LPCSTR lpTemplateName;
  HINSTANCE hInstance;
  LPSTR lpszStyle;
  WORD nFontType;
  INT nSizeMin, nSizeMax;
} CHOOSEFONT, CHOOSEFONTW, *LPCHOOSEFONT, *LPCHOOSEFONTW;

typedef struct tagOFN {
  DWORD lStructSize;
  HWND hwndOwner;
  HINSTANCE hInstance;
  LPCSTR lpstrFilter;
  LPSTR lpstrCustomFilter;
  DWORD nMaxCustFilter, nFilterIndex;
  LPSTR lpstrFile;
  DWORD nMaxFile;
  LPSTR lpstrFileTitle;
  DWORD nMaxFileTitle;
  LPCSTR lpstrInitialDir, lpstrTitle;
  DWORD Flags;
  WORD nFileOffset, nFileExtension;
  LPCSTR lpstrDefExt;
  LPARAM lCustData;
  LPOFNHOOKPROC lpfnHook;
  LPCSTR lpTemplateName;
} OPENFILENAME, OPENFILENAMEW, *LPOPENFILENAME, *LPOPENFILENAMEW;

#define CF_SCREENFONTS 0x00000001
#define CF_SHOWHELP 0x00000004
#define CF_ENABLEHOOK 0x00000008
#define CF_ENABLETEMPLATE 0x00000010
#define CF_INITTOLOGFONTSTRUCT 0x00000040
#define CF_LIMITSIZE 0x00002000
#define CF_ANSIONLY 0x00000400
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_PATHMUSTEXIST 0x00000800
