/**************************************************************************

   wfdrop.h

   Include for WINFILE program

   Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

   UnDOS: there is no OLE, so windows register no drop target and the
   OLE data-object code (wfdrop.c) is not built. Drag and drop inside
   WINFILE uses its own WM_DRAGLOOP/WM_DROPOBJECT messages.

**************************************************************************/

#ifndef WFDROP_INC
#define WFDROP_INC

typedef void IDropTarget;
typedef void WF_IDropTarget;

#define DROPEFFECT_NONE 0
#define DROPEFFECT_COPY 1
#define DROPEFFECT_MOVE 2
#define DROPEFFECT_LINK 4

static inline void RegisterDropWindow(HWND hwnd, WF_IDropTarget **ppDropTarget)
{
   (void)hwnd;
   *ppDropTarget = NULL;
}

static inline void UnregisterDropWindow(HWND hwnd, IDropTarget *pDropTarget)
{
   (void)hwnd;
   (void)pDropTarget;
}

#endif
