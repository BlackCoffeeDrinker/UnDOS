/********************************************************************

   tbar.c - UnDOS version

   Windows 3.1 File Manager had no toolbar and UnDOS has no comctl32
   toolbar control, so this file keeps the TBAR.C entry points that the
   rest of WINFILE calls but creates no toolbar: hwndToolbar stays NULL
   and bToolbar is FALSE.

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

********************************************************************/

#include "winfile.h"

VOID CreateFMToolbar(VOID) { bToolbar = FALSE; hwndToolbar = NULL; }

DWORD
DriveListMessage(UINT wMsg, WPARAM wParam, LPARAM lParam, UINT* puiRetVal)
{
   (void)wMsg; (void)wParam; (void)lParam;
   if (puiRetVal) *puiRetVal = 0;
   return FALSE;
}

VOID SelectToolbarDrive(DRIVEIND DriveInd) { (void)DriveInd; }
VOID FillToolbarDrives(DRIVE drive) { (void)drive; }
VOID EnableCheckTBButtons(HWND hwndActive) { (void)hwndActive; }
VOID CheckTBButton(DWORD idCommand) { (void)idCommand; }
VOID InitToolbarButtons(VOID) {}
VOID EnableDisconnectButton(VOID) {}
VOID EnableStopShareButton(VOID) {}
BOOL InitToolbarExtension(INT iExt) { (void)iExt; return FALSE; }
VOID FreeToolbarExtensions(VOID) {}
VOID SaveRestoreToolbar(BOOL bSave) { (void)bSave; }

// Drive line used by the Properties dialog: "<mount> <label>".
VOID
BuildDriveLine(LPTSTR* ppszTemp, INT driveInd, BOOL fGetFloppyLabel, DWORD dwType)
{
   static TCHAR szDrive[64];
   (void)fGetFloppyLabel; (void)dwType;
   wsprintf(szDrive, TEXT("%s "), UndosMountName(rgiDrive[driveInd]));
   *ppszTemp = szDrive;
}
