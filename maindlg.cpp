#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <tchar.h>
#include <winsock.h>
#include "AppDialog.h"
#include "resource.h"
#include "ParseHex.h"
#include "Trace.h"

#ifdef FTD2XX
    #include "ftd2xx.h"
#endif

#define ProgressBar_SetRange(hwnd,lo,hi)  SendMessage(hwnd, PBM_SETRANGE, 0, MAKELONG(lo,hi))
#define ProgressBar_SetPos(hwnd,pos)      SendMessage(hwnd, PBM_SETPOS, pos, 0L)

#define BUF_SIZE        2048

#define DEFAULT_COUNT   100
#define MAX_COUNT       1024

#define LOG_SENT_FLAG   0x80000000
#define LOG_COUNT_MASK  0x7FFFFFFF

#define GET_TIME_MSEC   timeGetTime

const TCHAR szSettings[] = "Settings";
const TCHAR szHeaderBytes[] = "HeaderBytes";
const TCHAR szTrailerBytes[] = "TrailerBytes";
const TCHAR szLengthMSB[] = "LengthMSB";
const TCHAR szLengthLSB[] = "LengthLSB";
TCHAR szProfile[MAX_PATH];

typedef struct list_item
{
    DWORD time;
    UINT count;
    BYTE data[BUF_SIZE];
} LIST_ITEM;

LIST_ITEM g_log_buf[MAX_COUNT];
DWORD g_start;
UINT g_logindex = 0;

void Log_AddItem(HWND hListView, LPVOID pBuffer, size_t count)
{
    int iItem = g_logindex++;
    LIST_ITEM* pLog = &g_log_buf[iItem % MAX_COUNT];

    pLog->time = GET_TIME_MSEC() - g_start;
    pLog->count = count;

    memcpy(pLog->data, pBuffer, count & LOG_COUNT_MASK);
    LVITEM lvi;
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_TEXT;
    lvi.pszText = LPSTR_TEXTCALLBACK;
    lvi.iItem = iItem + 1;
    int index = ListView_InsertItem(hListView, &lvi);
    ListView_EnsureVisible(hListView, index, /*bPartial=*/TRUE);
}

#define STR_MAX             80
#define DEFAULT_TIMEOUT     1000

#ifdef FTD2XX

FT_HANDLE W32_OpenDevice(LPTSTR pszFile, DWORD dwBaud)
{
    FT_HANDLE hDevice;

    if (FT_OpenEx(pszFile, FT_OPEN_BY_SERIAL_NUMBER, &hDevice) != FT_OK)
        return INVALID_HANDLE_VALUE;
    return hDevice;
}

BOOL W32_CloseDevice(FT_HANDLE handle)
{
    return FT_Close(handle) == FT_OK;
}

size_t W32_WriteBytes(FT_HANDLE hDevice, LPVOID pBuffer, size_t count)
{
    DWORD dwWritten = 0;
    if (hDevice != INVALID_HANDLE_VALUE) 
    {
        dwWritten = 0;
        BOOL bResult = FT_Write(hDevice, pBuffer, count, &dwWritten);
        TRACE4("W32_WriteBytes(%08x, %u)=%u dwWritten=%u\n", pBuffer, count, bResult, dwWritten);
    }
    return dwWritten;
}

size_t W32_ReadBytes(FT_HANDLE hDevice, LPVOID pBuffer, size_t count, DWORD timeout)
{
    DWORD tickStart = GET_TIME_MSEC();
    DWORD dwTotal = 0;

    if (timeout == 0)
        timeout = DEFAULT_TIMEOUT;
    
    /* exit loop after timeout has occurred */
    do
    {
        DWORD dwRead = 0;
        /*  Read bytes from serial port */
        if (FT_Read(hDevice, (LPSTR) pBuffer + dwTotal, count - dwTotal, &dwRead) != FT_OK)
        {
            DWORD dwError = FT_W32_GetLastError(hDevice);
            TRACE2("Error reading %08x: %d\n", hDevice, dwError);
            return dwRead;
        }

        dwTotal += dwRead;

        if (dwTotal >= count)
            break;
    }
    while (GET_TIME_MSEC() - tickStart <= timeout);
    return dwTotal;
}

int QueryCOMPorts(HWND hCombo)
{
    FT_STATUS status;
    DWORD dwNumDevs;
    
    status = FT_ListDevices(&dwNumDevs, NULL, FT_LIST_NUMBER_ONLY);
    if (status != FT_OK)
        return -1;

    ComboBox_ResetContent(hCombo);
    for (DWORD index = 0; index < dwNumDevs; ++index)
    {
        char szDevice[64];

        status = FT_ListDevices((PVOID) index, szDevice, FT_LIST_BY_INDEX|FT_OPEN_BY_SERIAL_NUMBER);
        if (status < FT_OK)
            return -2;

        ComboBox_AddString(hCombo, szDevice);
    }
    return dwNumDevs;
}

#else

typedef HANDLE FT_HANDLE;

//
//  Win32 wrapper functions
//
BOOL W32_SetupCommPort(HANDLE hDevice, DWORD baud, DWORD timeout)
{
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = timeout;
    timeouts.ReadTotalTimeoutConstant = timeout;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    BOOL bResult = SetCommTimeouts(hDevice, &timeouts);
    if (bResult && baud != 0)
    {
        DCB dcb;

        ::ZeroMemory(&dcb, sizeof(dcb));

        //
        // DCB settings not in the user's control
        //
        dcb.DCBlength = sizeof(dcb);
        dcb.fParity = TRUE;
        dcb.fBinary = TRUE;

        dcb.BaudRate = baud;
        dcb.ByteSize = 8;
        dcb.StopBits = ONESTOPBIT;
        dcb.Parity = NOPARITY;
        //
        // set new state
        //
        bResult = SetCommState(hDevice, &dcb);
    }
    return bResult;
}

FT_HANDLE W32_OpenDevice(LPTSTR szDevice, DWORD dwBaud)
{
    TCHAR szPort[STR_MAX];
    wsprintf(szDevice, "\\\\.\\%s", szDevice);

    FT_HANDLE hDevice = CreateFile(szPort, GENERIC_READ | GENERIC_WRITE, /*dwShare=*/0,
                    /* security=*/ NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        W32_SetupCommPort(hDevice, dwBaud, DEFAULT_TIMEOUT);
    }
	return hDevice;
}

BOOL W32_CloseDevice(FT_HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE)
        return CloseHandle(handle);
    return FALSE;
}

size_t W32_WriteBytes(FT_HANDLE hDevice, LPVOID pBuffer, size_t count)
{
    DWORD dwWritten = 0;
    if (hDevice != INVALID_HANDLE_VALUE) 
    {
        dwWritten = 0;
        BOOL bResult = WriteFile(hDevice, pBuffer, count, &dwWritten, /*lpOverlapped=*/NULL);
        TRACE4("W32_WriteBytes(%08x, %u)=%u dwWritten=%u\n", pBuffer, count, bResult, dwWritten);
    }
    return dwWritten;
}

size_t W32_ReadBytes(FT_HANDLE hDevice, LPVOID pBuffer, size_t count, DWORD timeout)
{
    DWORD tickStart = GET_TIME_MSEC();
    DWORD dwTotal = 0;

    if (timeout == 0)
        timeout = DEFAULT_TIMEOUT;
    
    /* exit loop after timeout has occurred */
    do
    {
        DWORD dwRead = 0;
        /*  Read bytes from serial port */
        if (!::ReadFile(hDevice, (LPSTR) pBuffer + dwTotal, count - dwTotal, &dwRead, NULL))
        {
            DWORD dwError = ::GetLastError();
            TRACE2("Error reading %08x: %d\n", hDevice, dwError);
            return dwRead;
        }

        dwTotal += dwRead;

        if (dwTotal >= count)
            break;
    }
    while (GET_TIME_MSEC() - tickStart <= timeout);
    return dwTotal;
}

BOOL IsNumeric(LPCSTR pszString)
{
    size_t len = strlen(pszString);
    BOOL bNumeric = FALSE;
    //What will be the return value from this function (assume the best)
    for (size_t u = 0; u < len; u++)
    {
        TCHAR ch = pszString[u];
        bNumeric = (ch >= '0' && ch <= '9');
        if (!bNumeric)
        {
            break;
        }
    }
    return bNumeric;
}

int QueryCOMPorts(HWND hCombo)
{
    int count = 0;
    DWORD dwSize = 8192;
    DWORD dwRet;
    do
    {
        TCHAR* pszDevices = (TCHAR*) malloc(dwSize);
        if (pszDevices == NULL)
        {
            return -1;
        }
        dwRet = QueryDosDevice(NULL, pszDevices, dwSize);

        if (dwRet)
        {
            size_t u = 0;
            ComboBox_ResetContent(hCombo);
            while (pszDevices[u] != _T('\0'))
            {
                //Get the current device name
                TCHAR* pszCurrentDevice = pszDevices + u;
                
                //If it looks like "COMX" then
                //add it to the array which will be returned
                size_t len = _tcslen(pszCurrentDevice);
                if (len > 3)
                {
                    TRACE1("dev=%s\n", pszCurrentDevice);
                    if ((_tcsnicmp(pszCurrentDevice, _T("COM"), 3) == 0) && IsNumeric(pszCurrentDevice + 3))
                    {
                        ComboBox_AddString(hCombo, pszCurrentDevice);
                        ++count;
                    }
                }
                
                //Go to next device name
                u += (len + 1);
            }
        }
        free(pszDevices);
        dwSize *= 2;
    }
    while (dwRet == 0);
    return count;
}

#endif

const UINT baud[] = { 57600, 115200, 230400, 921600, 3000000, 12000000 };

void FillBaudRates(HWND hWndCombo)
{
    for (UINT u = 0; u < sizeof(baud) / sizeof(UINT); ++u)
    {
        TCHAR szText[STR_MAX];
        wsprintf(szText, "%u", baud[u]);
        ComboBox_AddString(hWndCombo, szText);
    }
}


class CMainDlg : public CAppDialog
{
protected:
    HWND m_hListView;
    HWND m_hComboBaud;
    HWND m_hComboPort;
    HWND m_hComboData;
    FT_HANDLE m_hDevice;
    LPBYTE m_pDataFile;
    BOOL m_bLog;
    HWND m_hProgress;
	DWORD m_dwThreadId;
	UINT m_uRepeat;
	size_t m_uHeaderBytes;
	size_t m_uTrailerBytes;
	size_t m_uLengthMSB;
	size_t m_uLengthLSB;
public:
    CMainDlg(HINSTANCE hInst);
    ~CMainDlg();


    virtual BOOL OnInitDialog(WPARAM wParam, LPARAM lParam);
    virtual BOOL OnDrawItem(WPARAM wParam, LPDRAWITEMSTRUCT lParam);
    virtual BOOL OnCommand(WPARAM wId);
    virtual BOOL OnNotify(WPARAM wId, LPNMHDR nmhdr);

    void EnableControls(BOOL flag);
    void UpdateUI();

    void CommandResponse(LPBYTE txbuf, size_t count, LPBYTE rxbuf, size_t length, DWORD timeout);
    void DoSend(void);
    void FillCombo(LPCTSTR pszFilename);
    void DoClear(void);
    void GetCellRect(int col, LPCRECT pItemRect, LPRECT pCellRect);
    BOOL DrawListView(LPDRAWITEMSTRUCT lpDIS);
};

// Constructor
CMainDlg::CMainDlg(HINSTANCE hInst) : CAppDialog(hInst)
{
    m_hListView = m_hComboPort = m_hComboData = NULL;
    m_hDevice = INVALID_HANDLE_VALUE;
    m_pDataFile = NULL;

	m_bLog = TRUE;
}

// Destructor
CMainDlg::~CMainDlg()
{
    if (W32_CloseDevice(m_hDevice))
    {
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    if (m_pDataFile != NULL)
    {
        GlobalFreePtr(m_pDataFile);
        m_pDataFile = NULL;
    }
}

#define LIST_COLS      3
short   msgColWidth[LIST_COLS] = { 30, 60, 290 };


BOOL CMainDlg::OnInitDialog(WPARAM wParam, LPARAM lParam)
{
    // Set icons
    // Attach icon to main dialog
    HICON hIcon = LoadIcon(m_hInst, MAKEINTRESOURCE (IDD_MAIN));
    SendMessage(m_hWnd, WM_SETICON, TRUE, (LPARAM) hIcon);
    SendMessage (m_hWnd, WM_SETICON, FALSE, (LPARAM) hIcon);

    m_hListView = GetDlgItem(m_hWnd, IDC_LISTVIEW);
    m_hComboBaud = GetDlgItem(m_hWnd, IDC_BAUD_RATE);
    m_hComboPort = GetDlgItem(m_hWnd, IDC_PORT);
    m_hComboData = GetDlgItem(m_hWnd, IDC_DATA);
    m_hProgress = GetDlgItem(m_hWnd, IDC_PROGRESS);

    // Fill in combobox Baud rates
    FillBaudRates(m_hComboBaud);
    ComboBox_SetCurSel(m_hComboBaud, 0);

    // Initialize default baud rate & repeat count
    CheckDlgButton(m_hWnd, IDC_SET_BAUD, TRUE);
    SetDlgItemInt(m_hWnd, IDC_COUNT, DEFAULT_COUNT, FALSE);

	GetModuleFileName(NULL, szProfile, MAX_PATH);
	int len = lstrlen(szProfile);
	if (szProfile[len - 4] == '.')
	{
		lstrcpy(szProfile + len - 3, "ini");
	}

	m_uHeaderBytes = GetPrivateProfileInt(szSettings, szHeaderBytes, 4, szProfile);
	m_uTrailerBytes = GetPrivateProfileInt(szSettings, szTrailerBytes, 0, szProfile);
	m_uLengthMSB = GetPrivateProfileInt(szSettings, szLengthMSB, 2, szProfile);
	m_uLengthLSB =  GetPrivateProfileInt(szSettings, szLengthLSB, 3, szProfile);

    CheckDlgButton(m_hWnd, IDC_LOG, m_bLog);

    // Setup List View columns
    TCHAR szHeaders[STR_MAX];
    if (LoadString(m_hInst, IDS_LIST_HEADERS, szHeaders, STR_MAX))
    {
        LPTSTR pszText = szHeaders;
        for (int col = 0; col < LIST_COLS; ++col)
        {
            LPTSTR pszTab = strchr(pszText, '\t');
            if (pszTab != NULL)
            {
                *pszTab++ = '\0';
            }
            LV_COLUMN lvcol;
            ZeroMemory(&lvcol, sizeof(lvcol));
            lvcol.mask = LVCF_TEXT|LVCF_FMT|LVCF_WIDTH;
            lvcol.pszText = pszText;
            lvcol.fmt = LVCFMT_CENTER;
            lvcol.cx = msgColWidth[col];

            int index = ListView_InsertColumn(m_hListView, col + 1, &lvcol);
            pszText = pszTab;
        }
    }

    // Fill in USB device names
    if (QueryCOMPorts(m_hComboPort) > 0)
    {
        ComboBox_SetCurSel(m_hComboPort, 0);
    }

    UpdateUI();

    // Fill in combobox messages
#if defined(_DEBUG) && !defined(FTD2XX)
    FillCombo("dbgcmd.txt");
#else
    FillCombo("usbcmd.txt");
#endif

    DoClear();
    return TRUE;
}

void CMainDlg::EnableControls(BOOL flag)
{
    HWND hWnd= GetDlgItem(m_hWnd, IDC_SEND);
    EnableWindow(hWnd, !flag);
    EnableWindow(m_hComboPort, flag);
    hWnd = GetDlgItem(m_hWnd, IDC_BAUD_RATE);
    EnableWindow(hWnd, flag);
    hWnd = GetDlgItem(m_hWnd, IDC_SET_BAUD);
    EnableWindow(hWnd, flag);
}

void CMainDlg::UpdateUI()
{
    TCHAR szText[STR_MAX];
    UINT uId;
    if (m_hDevice == INVALID_HANDLE_VALUE)
    {
        uId = IDS_OPEN;
        EnableControls(TRUE);
    }
    else
    {
        uId = IDS_CLOSE;
        EnableControls(FALSE);
    }

    if (LoadString(m_hInst, uId, szText, STR_MAX))
    {
        SetDlgItemText(m_hWnd, IDC_OPEN_CLOSE, szText);
    }
}

void CMainDlg::CommandResponse(LPBYTE txbuf, size_t count, LPBYTE rxbuf, size_t length, DWORD timeout)
{
    size_t written;
    /* Flush receive buffer */
    PurgeComm(m_hDevice, PURGE_RXCLEAR);
    written = W32_WriteBytes(m_hDevice, txbuf, count);
    if (m_bLog && written > 0)
	{
        Log_AddItem(m_hListView, txbuf, written | LOG_SENT_FLAG);
	}

    size_t read = W32_ReadBytes(m_hDevice, rxbuf, m_uHeaderBytes, timeout);
    if (read == m_uHeaderBytes)
    {
        short length = rxbuf[m_uLengthMSB] * 256 + rxbuf[m_uLengthLSB];
        if (length >= 0)
        {
			length += m_uTrailerBytes;
			if (length > 0)
				read += W32_ReadBytes(m_hDevice, rxbuf + m_uHeaderBytes, length, timeout);
        }
    }
    if (m_bLog && read > 0)
	{
        Log_AddItem(m_hListView, rxbuf, read);
	}
}

ULONG __stdcall SendThread(LPVOID pThis)
{
	((CMainDlg*) pThis)->DoSend();
	return 0;
}

void CMainDlg::DoSend(void)
{
    TCHAR szText[4096];
    BYTE txbuf[BUF_SIZE];
    BYTE rxbuf[BUF_SIZE];
    int count;

    int index = ComboBox_GetCurSel(m_hComboData);
    if (index >= 0)
    {
        LPCTSTR itemdata = (LPCTSTR) ComboBox_GetItemData(m_hComboData, index);
        count = lstrlen(itemdata);
        memcpy(szText, itemdata, count + 1);
    }
    else
    {
        count = GetWindowText(m_hComboData, szText, BUF_SIZE);
    }

    if (count > 0)
    {
        count = ParseHexBuf(txbuf, szText);
        if (count >= 0)
        {
            BOOL bOK;
            m_uRepeat = IsDlgButtonChecked(m_hWnd, IDC_REPEAT) ? GetDlgItemInt(m_hWnd, IDC_COUNT, &bOK, FALSE) : 1U;
            const DWORD dwStart = GET_TIME_MSEC();
            ProgressBar_SetRange(m_hProgress, 0, m_uRepeat);
            for (UINT loop = 0; loop < m_uRepeat; ++loop)
            {
                CommandResponse(txbuf, count, rxbuf, BUF_SIZE, 0);
                ProgressBar_SetPos(m_hProgress, loop + 1);
                UpdateWindow(m_hProgress);
            }
            if (m_uRepeat > 1)
            {
                TCHAR szText[STR_MAX];
                DWORD dwElapsed = GET_TIME_MSEC() - dwStart;
                wsprintf(szText, "Elapsed=%u.%03u sec", dwElapsed / 1000, dwElapsed % 1000);
                MessageBox(m_hWnd, szText, "Info", MB_ICONINFORMATION);
            }
        }
    }
}

void CMainDlg::FillCombo(LPCTSTR pszFilename)
{
    HANDLE hFile = CreateFile(pszFilename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                       /* security=*/ NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD dwSize = GetFileSize(hFile, NULL);
        m_pDataFile = (LPBYTE) GlobalAllocPtr(GMEM_SHARE, dwSize);
        if (m_pDataFile != NULL)
        {
            DWORD dwRead = 0;

            ReadFile(hFile, m_pDataFile, dwSize, &dwRead, NULL);

            LPTSTR pszText = (LPTSTR) m_pDataFile;
            // Search for CR
            for (;;)
            {
                LPTSTR pEol = strchr(pszText, '\r');
                if (pEol == NULL)
                {
                    break;
                }
                *pEol++ = '\0';
                // Zero (optional) LF
                if (*pEol == '\n')
                {
                    *pEol++ = '\0';
                }

                // Check for ':' or '\t' deliminter
                LPTSTR pDelim = strchr(pszText, ':');
                if (pDelim == NULL)
                {
                    pDelim = strchr(pszText, '\t');
                }
                if (pDelim != NULL)
                {
                    *pDelim++ = '\0';
                }
                int index = ComboBox_InsertString(m_hComboData, -1, pszText);
                if (pDelim != NULL)
                {
                    ComboBox_SetItemData(m_hComboData, index, pDelim);
                }
                pszText = pEol;
            }

        }
        CloseHandle(hFile);
    }
}

BOOL CMainDlg::OnNotify(WPARAM wId, LPNMHDR nmhdr)
{
    TRACE2("OnNotify(%u) code=%08x\n", wId, nmhdr->code);
    switch (wId)
    {
    case IDC_DATA:
        break;
    }
    return TRUE;
}

#define LVCOLS  4

void CMainDlg::GetCellRect(int col, LPCRECT pItemRect, LPRECT pCellRect)
{
    HWND hwndHeader = ListView_GetHeader(m_hListView);
    RECT header_rect;
    Header_GetItemRect(hwndHeader, col, &header_rect);
    
    // If we don't do this, when we scroll to the right, we will be 
    // drawing as if we weren't and your cells won't line up with the
    // columns.
    int x_offset = GetScrollPos(m_hListView, SB_HORZ);
    pCellRect->left = header_rect.left - x_offset;
    pCellRect->right = header_rect.right - x_offset;
    pCellRect->top = pItemRect->top;
    pCellRect->bottom = pItemRect->bottom;
}

#define DT_FLAGS    (DT_END_ELLIPSIS | DT_VCENTER | DT_SINGLELINE)
#define LIGHT_RED   RGB(255,224,224)
#define LIGHT_GRN   RGB(224,255,224)

BOOL CMainDlg::DrawListView(LPDRAWITEMSTRUCT lpDIS)
{
    char szText[BUF_SIZE * 4];
    int count;
    int iItem = lpDIS->itemID;
    LIST_ITEM* pLog = &g_log_buf[iItem % MAX_COUNT];

    HDC hDC = lpDIS->hDC;
    
    COLORREF rgbBack, rgbText;
    BOOL bSelected = lpDIS->itemState & ODS_SELECTED;
    if (bSelected)
    {
        rgbBack = GetSysColor(COLOR_HIGHLIGHT);
        rgbText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    else
    {
        const BOOL bSent = (pLog->count & LOG_SENT_FLAG);
        short length = pLog->data[1];
        rgbBack = bSent ? GetSysColor(COLOR_WINDOW) : (length < 0) ? LIGHT_RED : LIGHT_GRN;
        rgbText = GetSysColor(COLOR_WINDOWTEXT);
    }
    
    COLORREF oldBack = SetBkColor(hDC, rgbBack);
    COLORREF oldText = SetTextColor(hDC, rgbText);
    
    // Fill background
    ExtTextOut(hDC, 0,0, ETO_OPAQUE | ETO_CLIPPED, &lpDIS->rcItem, NULL, 0, NULL);
    
    for (int col = 0; col < LVCOLS + 1; ++col)
    {
        RECT rect;
        GetCellRect(col, &lpDIS->rcItem, &rect);
        DWORD msec;
        int i, length = 0;
        
//        TRACE5("col=%d (%d,%d, %d,%d)\n", col, rect.left,rect.top, rect.right,rect.bottom);
        int iFormat = DT_FLAGS;
        

        switch (col)
        {
        case 0:
            length = wsprintf(szText, "%u", iItem);
            iFormat = DT_FLAGS | DT_CENTER;
            break;
        case 1:
            msec = pLog->time;
            length = wsprintf(szText, "%u.%03u ", msec / 1000, msec % 1000);
            iFormat = DT_FLAGS | DT_RIGHT;
            break;
        case 2:
            iFormat = DT_FLAGS | DT_LEFT;
            count = pLog->count & LOG_COUNT_MASK;
            if (count > BUF_SIZE)
                count = BUF_SIZE;
            i = 0;
            while (i < count)
            {
                int remain = count - i;
                if (remain >= 2)
                {
                    length += wsprintf(szText + length, " %04X", (pLog->data[i] << 8) | pLog->data[i + 1]);
                    i += 2;
                }
                else
                {
                    length += wsprintf(szText + length, " %02X", pLog->data[i++]);
                }
            }
        }
        // Draw Text
        DrawText(hDC, szText, length, &rect, iFormat);
    }
    // Restore DC
    SetBkColor(hDC, oldBack);
    SetTextColor(hDC, oldText);
    return TRUE;

}


BOOL CMainDlg::OnDrawItem(WPARAM wId, LPDRAWITEMSTRUCT lpDIS)
{
    LPRECT pRect = &lpDIS->rcItem;
    LPTSTR pText = (LPTSTR) lpDIS->itemData;

    switch (wId)
    {
    case IDC_DATA:
        TRACE5("OnDrawItem id=%u rect=(%d,%d,%d,%d)\n", lpDIS->itemID, pRect->left, pRect->top, pRect->right, pRect->bottom);
        if (pText != NULL)
        {
            LPCTSTR pDelim = strchr(pText, ':');
            int len = (pDelim != NULL) ? pDelim - pText : -1;
            DrawText(lpDIS->hDC, pText, len, pRect, DT_LEFT);
            TRACE1("OnDrawItem text=%s\n", pText);
        }
        break;
    case IDC_LISTVIEW:
        DrawListView(lpDIS);
        break;
    default:
        MsgBox(MB_ICONERROR, IDS_ERR_DRAWITEM, wId);
    }
    return TRUE;
}

void CMainDlg::DoClear()
{
	if (m_uRepeat <= 1)
	{
		ProgressBar_SetPos(m_hProgress, 0);
	}
	m_uRepeat = 0;	// Stop loop in progress
    ListView_DeleteAllItems(m_hListView);
    g_logindex = 0;
    g_start = GET_TIME_MSEC();
}


BOOL CMainDlg::OnCommand(WPARAM wId)
{
    WORD code = HIWORD(wId);
    WORD id = LOWORD(wId);
    TRACE2("OnCommand(code=%04x,id=%u\n", code, id);
    int index, result;
    LPCTSTR pszText;

    switch (id)
    {
    case IDC_OPEN_CLOSE:
        if (m_hDevice != INVALID_HANDLE_VALUE)
        {
            if (W32_CloseDevice(m_hDevice))
            {
                m_hDevice = INVALID_HANDLE_VALUE;
            }
        }
        else
        {
            TCHAR szPort[STR_MAX];
            if (GetDlgItemText(m_hWnd, IDC_PORT, szPort, STR_MAX))
            {
                const DWORD baud = IsDlgButtonChecked(m_hWnd, IDC_SET_BAUD) ? GetDlgItemInt(m_hWnd, IDC_BAUD_RATE, NULL, FALSE) : 0;
                m_hDevice = W32_OpenDevice(szPort, baud);
            }
        }
        UpdateUI();
        break;
    case IDC_SEND:
		// Start separate thread to transmit/receive messages
        CreateThread(NULL, /*stacksize=*/ 1000000, SendThread, this, 0/*run after creation*/, &m_dwThreadId);
        break;
    case IDC_CLEAR:
        DoClear();
        break;
	case IDC_LOG:
		if (code == BN_CLICKED)
		{
			m_bLog = IsDlgButtonChecked(m_hWnd, IDC_LOG);
		}
		break;
    case IDC_DATA:
        switch (code)
        {
        case CBN_SELCHANGE:
            index = ComboBox_GetCurSel(m_hComboData);
            pszText = (LPCTSTR) ComboBox_GetItemData(m_hComboData, index);
            TRACE2("CBN_SELCHANGE index=%d: %s\n", index, pszText);
            result = SendMessage(m_hComboData, WM_SETTEXT, 0, (LPARAM) pszText);
            return TRUE;
        case CBN_CLOSEUP:
            TRACE("CBN_CLOSEUP\n");
            return TRUE;
        case CBN_SELENDOK:
            TRACE("CBN_SELENDOK\n");
            return TRUE;
        default:
            ;
        }
        return FALSE;
    }
    return TRUE;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
    CMainDlg dlg(hInst);
 
    INITCOMMONCONTROLSEX InitCtrlEx;
 
    InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
    InitCtrlEx.dwICC  = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&InitCtrlEx);

    if (dlg.CreateDialogBox(MAKEINTRESOURCE(IDD_MAIN), /*hParent=*/NULL))
    {
        dlg.ShowWindow(nCmdShow);
        return dlg.Run();
    }
    return FALSE;
}
