#include <windows.h>
#include "resource.h"
#include "AppDialog.h"
#include "trace.h"

// Definitions copied from dlgfont.c
// Source: http://github.com/strobejb/HexEdit/blob/master/src/HexEdit/dlgfont.c

#pragma pack(push, 1)

typedef struct dlg_template_ex
{  
  WORD      dlgVer; 
  WORD      signature; 
  DWORD     helpID; 
  DWORD     exStyle; 
  DWORD     style; 
  WORD      cDlgItems; 
  short     x; 
  short     y; 
  short     cx; 
  short     cy;
} DLGTEMPLATEEX;

typedef struct dlg_item_template_ex
{ 
  DWORD  helpID; 
  DWORD  exStyle; 
  DWORD  style; 
  short  x; 
  short  y; 
  short  cx; 
  short  cy; 
  WORD   id; 
  WORD	 reserved;		// Q141201 - there is an extra WORD here

} DLGITEMTEMPLATEEX;

typedef struct dlg_template_font
{
  WORD     pointsize; 
  WORD     weight; 
  BYTE     italic;
  BYTE     charset; 
  WCHAR    typeface[1];  

} DLGTEMPLATEEXFONT;

#pragma pack(pop)

// Patch for older windows.h SDK header file
#ifndef DS_FIXEDSYS
    #define DS_FIXEDSYS         0x0008L
#endif

#define ALIGN32(p)      (((DWORD) (p) + 3) & ~3)

// Constructor
CAppDialog::CAppDialog(HINSTANCE hInst)
{
    m_hInst = hInst;
    m_hWnd = NULL;
    LoadString(hInst, IDS_APP_NAME, m_szAppName, STR_MAX);
	m_iResizeCount = 0;
	m_pResizeInfo = NULL;
}

BOOL CAppDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

BOOL CAppDialog::OnInitDialog(WPARAM wParam, LPARAM lParam)
{
    return TRUE;
}

BOOL CAppDialog::OnDrawItem(WPARAM wParam, LPDRAWITEMSTRUCT lParam)
{
    return TRUE;
}

BOOL CAppDialog::OnCommand(WPARAM wId)
{
    return TRUE;
}

BOOL CAppDialog::OnNotify(WPARAM wId, LPNMHDR nmhdr)
{
    return TRUE;
}

void CAppDialog::MsgBox(UINT type, UINT id, ...)
{
    va_list arglist;

    TCHAR szFormat[80];
    ::LoadString(m_hInst, id, szFormat, sizeof(szFormat));

    TCHAR szBuffer[256];
    va_start(arglist, id );
    wvsprintf(szBuffer, szFormat, arglist);
    va_end(arglist);
    MessageBox(m_hWnd, szBuffer, m_szAppName, type);
}

void CAppDialog::SleepYield(DWORD msec)
{
    MSG msg;
    ::Sleep(msec);
    if (PeekMessage(&msg, m_hWnd, 0L, 0L, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

BOOL CAppDialog::OnGetMinMaxInfo(LPMINMAXINFO pInfo)
{
    pInfo->ptMinTrackSize.x = m_InitialSize.cx;
    pInfo->ptMinTrackSize.y = m_InitialSize.cy;
    return TRUE;
}

BOOL CAppDialog::OnSize(WPARAM wParam, LPARAM lParam)
{
    RECT rect;
    SIZE newSize;
    newSize.cx = LOWORD(lParam);
    newSize.cy = HIWORD(lParam);
    ::GetClientRect(m_hWnd, &rect);
	if (newSize.cx > 0 && newSize.cy > 0)
	{
		HWND hChild = ::GetWindow(m_hWnd, GW_CHILD);
		int count = m_iResizeCount;
		SIZE delta;
		TRACE4("OnSize: new=%d x %d old=%d x %d\n", newSize.cx, newSize.cy, m_oldSize.cx, m_oldSize.cy);
		delta.cx = newSize.cx - m_oldSize.cx;
		delta.cy = newSize.cy - m_oldSize.cy;

		HDWP hdwp = ::BeginDeferWindowPos(count);

		for (int i = 0; i < count; ++i)
		{
			const RESIZE_INFO* pResizeInfo = m_pResizeInfo + i;
			int id = pResizeInfo->id;
			UINT flags = pResizeInfo->flags;
			HWND hChild = GetDlgItem(m_hWnd, id);
			if (hChild != NULL)
			{
				::GetWindowRect(hChild, &rect);
				::ScreenToClient(m_hWnd, (LPPOINT) &rect.left);
				::ScreenToClient(m_hWnd, (LPPOINT) &rect.right);
				if (flags & RESIZE_X)
				{
					rect.left += delta.cx;
					rect.right += delta.cx;
				}
				if (flags & RESIZE_Y)
				{
					rect.top += delta.cy;
					rect.bottom += delta.cy;
				}
				if (flags & RESIZE_W)
				{
					rect.right += delta.cx;
				}
				if (flags & RESIZE_H)
				{
					rect.bottom += delta.cy;
				}
				int x = rect.left;
				int y = rect.top;
				int w = rect.right - rect.left;
				int h = rect.bottom - rect.top;
				::DeferWindowPos(hdwp, hChild, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
				TRACE6("ReizeWhildWindows: id=%d x=%d, y=%d, w=%d, h=%d help=%u\n", id, x, y, w, h, flags);
			}
		}
		::EndDeferWindowPos(hdwp);
		// Update size for next time
		m_oldSize = newSize;
	}
    return TRUE;
}

void CAppDialog::OnClose()
{
    ::EndDialog(m_hWnd, IDOK);
}

BOOL CALLBACK AppDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CAppDialog* pDlg = (CAppDialog*) GetWindowLong(hDlg, GWL_USERDATA);
    RECT rect;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetWindowLong(hDlg, GWL_USERDATA, lParam);
        pDlg = (CAppDialog*) lParam;
        pDlg->m_hWnd = hDlg;
        GetWindowRect(hDlg, &rect);
        SIZE size;
        size.cx = rect.right - rect.left;
        size.cy = rect.bottom - rect.top;
        pDlg->m_oldSize = pDlg->m_InitialSize = size;
        return pDlg->OnInitDialog(wParam, lParam);
    case WM_GETMINMAXINFO:
        return pDlg->OnGetMinMaxInfo((LPMINMAXINFO) lParam);
    case WM_SIZE:
        return pDlg->OnSize(wParam, lParam);
    case WM_CLOSE:
        pDlg->OnClose();
        DestroyWindow(hDlg);
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_COMMAND:
        return pDlg->OnCommand(wParam);
    case WM_NOTIFY:
        return pDlg->OnNotify(wParam, (LPNMHDR) lParam);
    case WM_DRAWITEM:
        return pDlg->OnDrawItem(wParam, (LPDRAWITEMSTRUCT) lParam);
    return pDlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

BOOL CAppDialog::CreateDialogBox(LPCTSTR lpszResource, HWND hParent)
{
    m_hWnd = CreateDialogParam(m_hInst, lpszResource, hParent, (DLGPROC)AppDialogProc, (LPARAM) this);
    return m_hWnd != NULL;
}

int CAppDialog::Run(void)
{
    int status;
    MSG msg;

    while ((status = GetMessage(&msg, 0, 0, 0)) != 0)
    {
        if (status == -1)
            return -1;
        if (!::IsDialogMessage(m_hWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return msg.wParam;
}

BOOL CAppDialog::ShowWindow(int nCmdShow)
{
    return ::ShowWindow(m_hWnd, nCmdShow);
}
