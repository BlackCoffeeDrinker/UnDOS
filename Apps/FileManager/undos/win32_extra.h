/* Win32 constants and types the WINFILE port names but the UnDOS SDK does
   not (yet) provide. Values are the real Win32 ones. Anything that turns out
   generally useful moves into the SDK headers with a test. */
#ifndef UNDOS_WIN32_EXTRA_H
#define UNDOS_WIN32_EXTRA_H

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* --- Wide-string names: TCHAR is char on UnDOS, so these are the char
   string functions. */
#define wcslen strlen
#define wcscmp strcmp
#define wcsncmp strncmp
#define wcsrchr strrchr
#define wcschr strchr
#define _wcsicmp lstrcmpi
#define wcscpy_s(d, n, s) (lstrcpyn((d), (s), (int)(n)), 0)
#define wcsncpy_s(d, n, s, c) \
  (lstrcpyn((d), (s), (int)((size_t)(c) < (size_t)(n) ? (size_t)(c) + 1 : (size_t)(n))), 0)
#define wcsncat_s(d, n, s, c) (strncat((d), (s), (c)), 0)
#define swprintf_s(d, n, ...) wsprintf((d), __VA_ARGS__)
#define StrChr strchr
#define StrRChr(start, end, c) strrchr((start), (c))
#define StrCpyN lstrcpyn

/* --- W/A API names: no wide characters, so both are the plain call. ---- */
#define WNDCLASSW WNDCLASS
#define RegisterClassW RegisterClass
#define RegisterClassA RegisterClass
#define InsertMenuW InsertMenu
#define InsertMenuA InsertMenu
#define GetClassNameW GetClassName
#define GetClassNameA GetClassName
#define GetLocaleInfoW GetLocaleInfo
#define GetNumberFormatW GetNumberFormat
#define GetDateFormatW GetDateFormat
#define GetTimeFormatW GetTimeFormat

/* --- Types ------------------------------------------------------------- */
typedef char *PCHAR, *LPTCH, *LPWCH;
typedef unsigned char UCHAR, TUCHAR, *PTUCHAR;
typedef UINT *LPUINT;
typedef HANDLE HDWP;
typedef struct _SMALL_RECT { SHORT Left, Top, Right, Bottom; } SMALL_RECT;

typedef struct tagWINDOWPLACEMENT {
  UINT length, flags, showCmd;
  POINT ptMinPosition, ptMaxPosition;
  RECT rcNormalPosition;
} WINDOWPLACEMENT, *PWINDOWPLACEMENT, *LPWINDOWPLACEMENT;

typedef struct tagMINMAXINFO {
  POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO, *LPMINMAXINFO;

typedef struct tagCOMPAREITEMSTRUCT {
  UINT CtlType, CtlID;
  HWND hwndItem;
  UINT itemID1;
  ULONG_PTR itemData1;
  UINT itemID2;
  ULONG_PTR itemData2;
  DWORD dwLocaleId;
} COMPAREITEMSTRUCT, *LPCOMPAREITEMSTRUCT;

typedef struct tagNMHDR { HWND hwndFrom; UINT_PTR idFrom; UINT code; } NMHDR, *LPNMHDR;

#define LF_FACESIZE 32
#define LF_FULLFACESIZE 64
typedef struct tagLOGFONT {
  LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
  BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
  BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
  char lfFaceName[LF_FACESIZE];
} LOGFONT, *PLOGFONT, *LPLOGFONT;

typedef BITMAPINFO *LPBITMAPINFO;

typedef struct _OSVERSIONINFO {
  DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
  char szCSDVersion[128];
} OSVERSIONINFO, *LPOSVERSIONINFO;

typedef struct tagVS_FIXEDFILEINFO {
  DWORD dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS;
  DWORD dwProductVersionMS, dwProductVersionLS, dwFileFlagsMask, dwFileFlags;
  DWORD dwFileOS, dwFileType, dwFileSubtype, dwFileDateMS, dwFileDateLS;
} VS_FIXEDFILEINFO;

typedef struct _BY_HANDLE_FILE_INFORMATION {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks;
  DWORD nFileIndexHigh, nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

typedef WIN32_FIND_DATA *PWIN32_FIND_DATA;

typedef struct _NUMBERFMT {
  UINT NumDigits, LeadingZero, Grouping;
  LPSTR lpDecimalSep, lpThousandSep;
  UINT NegativeOrder;
} NUMBERFMT;

/* --- File attributes and file API flags -------------------------------- */
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000
#define IO_REPARSE_TAG_RESERVED_ZERO 0
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003L
#define IO_REPARSE_TAG_SYMLINK 0xA000000CL
#define IsReparseTagMicrosoft(t) (((t) & 0x80000000) != 0)
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE (16 * 1024)
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#define COPY_FILE_COPY_SYMLINK 0x00000800
#define FILE_NOTIFY_CHANGE_FILE_NAME 0x001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x004
#define FILE_NOTIFY_CHANGE_SIZE 0x008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x010
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define FILE_SHARE_DELETE 0x4
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_READ_EA 0x0008
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define OPEN_EXISTING 3
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
#define FS_CASE_IS_PRESERVED 0x00000002
#define FS_FILE_COMPRESSION 0x00000010
#define FS_VOL_IS_COMPRESSED 0x00008000
#define FS_FILE_ENCRYPTION 0x00020000
#define FSCTL_SET_COMPRESSION 0x9C040
#define FSCTL_GET_REPARSE_POINT 0x900A8
#define FSCTL_SET_REPARSE_POINT 0x900A4
typedef enum { FindExInfoStandard } FINDEX_INFO_LEVELS;
typedef enum { FindExSearchNameMatch, FindExSearchLimitToDirectories } FINDEX_SEARCH_OPS;

/* --- Error codes -------------------------------------------------------- */
#define NO_ERROR 0
#define ERROR_INVALID_DATA 13
#define ERROR_OUTOFMEMORY 14
#define ERROR_WRITE_PROTECT 19
#define ERROR_NOT_READY 21
#define ERROR_WRONG_DISK 34
#define ERROR_SHARING_VIOLATION 32
#define ERROR_NOT_SUPPORTED 50
#define ERROR_BAD_NET_NAME 67
#define ERROR_NETWORK_ACCESS_DENIED 65
#define ERROR_INVALID_SHARENAME 1215
#define ERROR_DISK_FULL 112
#define ERROR_CHILD_NOT_COMPLETE 129
#define ERROR_INVALID_SEGDPL 198
#define ERROR_INVALID_ORDINAL 182
#define ERROR_INVALID_EXE_SIGNATURE 191
#define ERROR_EXE_MARKED_INVALID 192
#define ERROR_BAD_EXE_FORMAT 193
#define ERROR_ITERATED_DATA_EXCEEDS_64k 194
#define ERROR_INVALID_MINALLOCSIZE 195
#define ERROR_INVALID_MODULETYPE 190
#define ERROR_INVALID_STARTING_CODESEG 188
#define ERROR_INVALID_STACKSEG 189
#define ERROR_RELOC_CHAIN_XEEDS_SEGLIM 201
#define ERROR_INFLOOP_IN_RELOC_CHAIN 202
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_MR_MID_NOT_FOUND 317
#define ERROR_INVALID_REPARSE_DATA 4392
#define ERROR_DLL_INIT_FAILED 1114
#define ERROR_NO_DATA_DETECTED 1104
#define ERROR_UNRECOGNIZED_VOLUME 1005
#define ERROR_CANT_ACCESS_FILE 1920
#define ERROR_EXTENDED_ERROR 1208
#define ERROR_NO_NETWORK 1222
#define ERROR_CONNECTION_UNAVAIL 1201
#define ERROR_NOT_CONNECTED 2250
#define ERROR_PRIVILEGE_NOT_HELD 1314
#define ERROR_RESOURCE_LANG_NOT_FOUND 1815
#define ERROR_SYMLINK_CLASS_DISABLED 1463

/* --- Window messages and styles ---------------------------------------- */
#define WM_SETREDRAW 0x000B
#define WM_QUERYENDSESSION 0x0011
#define WM_ENDSESSION 0x0016
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_WININICHANGE 0x001A
#define WM_CANCELMODE 0x001F
#define WM_GETMINMAXINFO 0x0024
#define WM_ICONERASEBKGND 0x0027
#define WM_CHARTOITEM 0x002F
#define WM_VKEYTOITEM 0x002E
#define WM_QUERYDRAGICON 0x0037
#define WM_COMPAREITEM 0x0039
#define WM_NOTIFY 0x004E
#define WM_CONTEXTMENU 0x007B
#define WM_SETICON 0x0080
#define WM_NCDESTROY 0x0082
#define WM_NCACTIVATE 0x0086
#define WM_MENUSELECT 0x011F
#define WM_LBTRACKPOINT 0x0131
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_LAYOUTRTL 0x00400000L
#define WS_EX_NOINHERITLAYOUT 0x00100000L
#define GWL_EXSTYLE (-20)
#define GWLP_HINSTANCE (-6)
#define GWLP_HWNDPARENT (-8)
#define SW_NORMAL 1
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SIZENORMAL 0
#define SIZEICONIC 1
#define SWP_NOCOPYBITS 0x0100
#define ICON_SMALL 0
#define MSGF_DIALOGBOX 0
#define MSGF_MENU 2
#define WH_MSGFILTER (-1)
#define MF_SYSMENU 0x2000
#define TPM_LEFTBUTTON 0x0000
#define TPM_LEFTALIGN 0x0000
#define TPM_RETURNCMD 0x0100
#define VK_LBUTTON 0x01
#define COLOR_APPWORKSPACE 12
#define SM_CXSCREEN 0
#define SM_CXFRAME 32
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SPI_GETWORKAREA 0x0030
#define LAYOUT_RTL 0x1
#define LAYOUT_BITMAPORIENTATIONPRESERVED 0x8
#define NOMIRRORBITMAP 0x80000000u
#define DI_NORMAL 0x0003
#define EM_LINELENGTH 0x00C1

/* --- List box and combo box -------------------------------------------- */
#define LBS_MULTICOLUMN 0x0200L
#define LB_SELITEMRANGE 0x019B
#define LB_GETHORIZONTALEXTENT 0x0193
#define LB_SETHORIZONTALEXTENT 0x0194
#define LB_SETCOLUMNWIDTH 0x0195
#define LB_SETANCHORINDEX 0x019C
#define LB_GETANCHORINDEX 0x019D
#define LB_ITEMFROMPOINT 0x01A9
#define CB_ERR (-1)
#define CB_GETEDITSEL 0x0140
#define CB_ADDSTRING 0x0143
#define CB_DELETESTRING 0x0144
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_GETLBTEXT 0x0148
#define CB_INSERTSTRING 0x014A
#define CB_RESETCONTENT 0x014B
#define CB_FINDSTRING 0x014C
#define CB_SETCURSEL 0x014E
#define CB_SHOWDROPDOWN 0x014F
#define CB_GETITEMDATA 0x0150
#define CB_SETITEMDATA 0x0151
#define CB_SETEXTENDEDUI 0x0155
#define CB_GETDROPPEDSTATE 0x0157
#define CBN_SELCHANGE 1
#define CBN_SETFOCUS 3
#define CBN_CLOSEUP 8

/* --- Fonts and GDI ------------------------------------------------------ */
#define ANSI_CHARSET 0
#define SHIFTJIS_CHARSET 128
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define DEFAULT_PITCH 0
#define VARIABLE_PITCH 2
#define FF_SWISS (2 << 4)
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define CBM_INIT 0x04L
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))

/* --- Locale ------------------------------------------------------------- */
#define CP_ACP 0
#define MB_PRECOMPOSED 0x1
#define NORM_IGNORECASE 0x1
#define LOCALE_USER_DEFAULT 0x0400
#define LOCALE_SDECIMAL 0x0E
#define LOCALE_STHOUSAND 0x0F
#define LOCALE_SLOCALIZEDDISPLAYNAME 0x02
#define DATE_SHORTDATE 0x1
#define TIME_NOTIMEMARKER 0x4
#define TIME_FORCE24HOURFORMAT 0x8
#define SORT_DEFAULT 0
#define LANG_ARABIC 0x01
#define LANG_CHINESE 0x04
#define LANG_GERMAN 0x07
#define LANG_ENGLISH 0x09
#define LANG_HEBREW 0x0D
#define LANG_JAPANESE 0x11
#define LANG_KOREAN 0x12
#define LANG_POLISH 0x15
#define LANG_PORTUGUESE 0x16
#define LANG_TURKISH 0x1F
#define SUBLANG_ENGLISH_US 0x01
#define SUBLANG_CHINESE_SIMPLIFIED 0x02
#define SUBLANG_GERMAN 0x01
#define SUBLANG_HEBREW_ISRAEL 0x01
#define SUBLANG_JAPANESE_JAPAN 0x01
#define SUBLANG_KOREAN 0x01
#define SUBLANG_POLISH_POLAND 0x01
#define SUBLANG_PORTUGUESE 0x02
#define SUBLANG_TURKISH_TURKEY 0x01

/* --- Threads, help, messages ------------------------------------------- */
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define HELP_CONTEXT 0x0001
#define HELP_QUIT 0x0002
#define HELP_INDEX 0x0003
#define HELP_HELPONHELP 0x0004
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x0200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x00FF
#define _TRUNCATE ((size_t)-1)

#endif
