/* WINFILE-style "Attributes" dialog built from an in-memory DLGTEMPLATE and
 * run with DialogBoxParam: an edit box, check boxes, radio buttons, a list
 * box and OK/Cancel. */
#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>

#define IDD_ATTRIBS 200
#define IDD_NAME 201
#define IDD_READONLY 202
#define IDD_HIDDEN 203
#define IDD_SORTNAME 204
#define IDD_SORTSIZE 205
#define IDD_FILES 206

static WORD templateWords[512];
static UINT used;

static void word(WORD value) {
  if (used < sizeof(templateWords) / sizeof(templateWords[0]))
    templateWords[used++] = value;
}

static void dword(DWORD value) {
  word((WORD)(value & 0xFFFFu));
  word((WORD)(value >> 16));
}

static void text(const char *value) {
  for (; *value; ++value) word((WORD)(unsigned char)*value);
  word(0);
}

static void item(DWORD style, short x, short y, short cx, short cy, WORD id,
                 WORD atom, const char *title) {
  if (used % 2) word(0);
  dword(style | WS_CHILD | WS_VISIBLE);
  dword(0);
  word((WORD)x);
  word((WORD)y);
  word((WORD)cx);
  word((WORD)cy);
  word(id);
  word(0xFFFF);
  word(atom);
  text(title);
  word(0);
}

static void build_template(void) {
  dword(WS_CAPTION | DS_MODALFRAME | DS_CENTER);
  dword(0);
  word(10);
  word(0);
  word(0);
  word(180);
  word(110);
  word(0);
  word(0);
  text("Properties");
  item(SS_LEFT, 6, 8, 30, 10, (WORD)-1, 0x0082, "&Name:");
  item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 40, 6, 134, 12, IDD_NAME,
       0x0081, "README.TXT");
  item(BS_GROUPBOX, 6, 22, 80, 36, (WORD)-1, 0x0080, "Attributes");
  item(BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 12, 32, 70, 10, IDD_READONLY,
       0x0080, "&Read Only");
  item(BS_AUTOCHECKBOX | WS_TABSTOP, 12, 44, 70, 10, IDD_HIDDEN, 0x0080,
       "&Hidden");
  item(BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 94, 32, 80, 10,
       IDD_SORTNAME, 0x0080, "Sort by &Name");
  item(BS_AUTORADIOBUTTON, 94, 44, 80, 10, IDD_SORTSIZE, 0x0080,
       "Sort by &Size");
  item(LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 6, 62,
       120, 42, IDD_FILES, 0x0083, "");
  item(BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 134, 62, 40, 14, IDOK,
       0x0080, "OK");
  item(BS_PUSHBUTTON | WS_TABSTOP, 134, 82, 40, 14, IDCANCEL, 0x0080,
       "Cancel");
}

static INT_PTR CALLBACK attributes_proc(HWND dialog, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
  (void)lParam;
  if (message == WM_INITDIALOG) {
    static const char *const files[] = {"WINFILE.EXE", "README.TXT",
                                        "CONFIG.SYS", "AUTOEXEC.BAT"};
    for (UINT i = 0; i < 4; ++i)
      (void)SendDlgItemMessage(dialog, IDD_FILES, LB_ADDSTRING, 0,
                               (LPARAM)files[i]);
    (void)SendDlgItemMessage(dialog, IDD_FILES, LB_SETCURSEL, 1, 0);
    (void)CheckDlgButton(dialog, IDD_READONLY, BST_CHECKED);
    (void)CheckRadioButton(dialog, IDD_SORTNAME, IDD_SORTSIZE, IDD_SORTNAME);
    return TRUE;
  }
  if (message == WM_COMMAND) {
    const WORD id = LOWORD(wParam);
    if (id == IDD_FILES && HIWORD(wParam) == LBN_SELCHANGE) {
      char name[32];
      const LRESULT index =
          SendDlgItemMessage(dialog, IDD_FILES, LB_GETCURSEL, 0, 0);
      if (index >= 0 &&
          SendDlgItemMessage(dialog, IDD_FILES, LB_GETTEXT, (WPARAM)index,
                             (LPARAM)name) >= 0)
        (void)SetDlgItemText(dialog, IDD_NAME, name);
      return TRUE;
    }
    if (id == IDOK || id == IDCANCEL) {
      char name[64];
      (void)GetDlgItemText(dialog, IDD_NAME, name, sizeof(name));
      (void)crt_printf("dialog: %s name=%s readonly=%u hidden=%u\n",
                       id == IDOK ? "ok" : "cancel", name,
                       IsDlgButtonChecked(dialog, IDD_READONLY),
                       IsDlgButtonChecked(dialog, IDD_HIDDEN));
      (void)EndDialog(dialog, id);
      return TRUE;
    }
  }
  return FALSE;
}

int main(void) {
  build_template();
  const UNDOS_RESOURCE resources[] = {
      {RT_DIALOG, MAKEINTRESOURCE(IDD_ATTRIBS), templateWords,
       used * (DWORD)sizeof(WORD)},
  };
  if (!UndosRegisterResources(resources, 1)) return 1;
  const INT_PTR result = DialogBoxParam(NULL, MAKEINTRESOURCE(IDD_ATTRIBS),
                                        NULL, attributes_proc, 0);
  (void)crt_printf("dialog: result=%ld\n", (long)result);
  return result == -1 ? 1 : 0;
}
