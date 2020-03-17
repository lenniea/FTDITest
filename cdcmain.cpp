#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <commctrl.h>
#include <tchar.h>
#include <winsock.h>
#include "AppDialog.h"
#include "resource.h"
#include "ParseHex.h"
#include "raw2bmp.h"
#include "Trace.h"

#ifdef FTD2XX
    #include "ftd2xx.h"

#define CBUS_BITBANG    0x20
#define SYNC_FIFO       0x40

#endif

#define ProgressBar_SetRange(hwnd,lo,hi)  SendMessage(hwnd, PBM_SETRANGE, 0, MAKELONG(lo,hi))
#define ProgressBar_SetPos(hwnd,pos)      SendMessage(hwnd, PBM_SETPOS, pos, 0L)

#define BUF_SIZE        65536

#define DEFAULT_COUNT   100
#define MAX_COUNT       1024
#define DEFAULT_HEADER	64896

#define LOG_SENT_FLAG   0x80000000
#define LOG_COUNT_MASK  0x7FFFFFFF

#define COUNT_OF(a)		(sizeof(a) / sizeof(a[0]))

#define GET_TIME_MSEC   timeGetTime

const TCHAR szSettings[] = "Settings";
const TCHAR szTimeOut[] = "TimeOut";
const TCHAR szHeaderBytes[] = "HeaderBytes";

TCHAR szProfile[MAX_PATH];
TCHAR szFilename[MAX_PATH];

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
UINT g_uTimeOut = DEFAULT_TIMEOUT;

#ifdef FTD2XX

FT_HANDLE W32_OpenDevice(LPTSTR pszFile, DWORD dwBaud)
{
    FT_HANDLE hDevice;
    FT_STATUS status;
    ULONG bufsize = 2048;

    status = FT_OpenEx(pszFile, FT_OPEN_BY_SERIAL_NUMBER, &hDevice);
    if (status != FT_OK)
        return INVALID_HANDLE_VALUE;

 
    if (dwBaud == 0)
    {
        // Set Syncrhonous 245 Mode
        status = FT_SetBitMode(hDevice, 0x00, SYNC_FIFO);
        if (status != FT_OK)
        {
            return INVALID_HANDLE_VALUE;
        }
        bufsize = 65536;

    }
    else
    {
        // Set 8 data bits, 1 stop bit and no parity
        status = FT_SetBaudRate(hDevice, dwBaud);
        if (status != FT_OK)
            return INVALID_HANDLE_VALUE;

        status = FT_SetDataCharacteristics
            (hDevice, FT_BITS_8, FT_STOP_BITS_1, FT_PARITY_NONE);
        if (status != FT_OK)
            return INVALID_HANDLE_VALUE;

        // Set CBUS to output all 0s
        status = FT_SetBitMode(hDevice, 0xF0, CBUS_BITBANG);
        if (status != FT_OK)
            return INVALID_HANDLE_VALUE;

        bufsize = 4096;
    }

    status = FT_SetLatencyTimer(hDevice, 2);
    if (status != FT_OK)
        return INVALID_HANDLE_VALUE;

    status = FT_SetUSBParameters(hDevice, bufsize, bufsize);
    if (status != FT_OK)
        return INVALID_HANDLE_VALUE;

    status = FT_SetTimeouts(hDevice, g_uTimeOut, g_uTimeOut);
    if (status != FT_OK)
        return INVALID_HANDLE_VALUE;

    return hDevice;
}

BOOL W32_CloseDevice(FT_HANDLE handle, DWORD dwBaud)
{
    if (handle != INVALID_HANDLE_VALUE && dwBaud != 0)
    {
        // Set CBUS back to all inputs
        FT_STATUS status = FT_SetBitMode(handle, 0x00, CBUS_BITBANG);
    }
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
        timeout = g_uTimeOut;
    
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
    wsprintf(szPort, "\\\\.\\%s", szDevice);

    FT_HANDLE hDevice = CreateFile(szPort, GENERIC_READ | GENERIC_WRITE, /*dwShare=*/0L,
                    /* security=*/ NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        W32_SetupCommPort(hDevice, dwBaud, g_uTimeOut);
    }
    return hDevice;
}

BOOL W32_CloseDevice(FT_HANDLE handle, DWORD dwBaud)
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
        timeout = g_uTimeOut;
    
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

class CMainDlg : public CAppDialog
{
protected:
    HWND m_hListView;
    HWND m_hComboPort;
    FT_HANDLE m_hDevice;
    LPBYTE m_pDataFile;
	DWORD m_dwDataSize;
    BOOL m_bLog;
    DWORD m_dwThreadId;
    UINT m_uRepeat;
    size_t m_uHeaderBytes;
	BYTE m_rxbuf[BUF_SIZE];
	BYTE m_image[BUF_SIZE];
public:
    CMainDlg(HINSTANCE hInst);
    ~CMainDlg();


    virtual BOOL OnInitDialog(WPARAM wParam, LPARAM lParam);
    virtual BOOL OnDrawItem(WPARAM wParam, LPDRAWITEMSTRUCT lParam);
    virtual BOOL OnCommand(WPARAM wId);
    virtual BOOL OnNotify(WPARAM wId, LPNMHDR nmhdr);

    void EnableControls(BOOL flag);
    void UpdateUI();

    size_t CommandResponse(LPBYTE txbuf, size_t count, LPBYTE rxbuf, size_t length, DWORD timeout);
    void DoSend(void);
	void ReadBinaryFile(LPCTSTR pszFilename);

    BOOL DrawView(LPDRAWITEMSTRUCT lpDIS);
};

// Constructor
CMainDlg::CMainDlg(HINSTANCE hInst) : CAppDialog(hInst)
{
    m_hListView = m_hComboPort = NULL;
    m_hDevice = INVALID_HANDLE_VALUE;
    m_pDataFile = NULL;

    m_bLog = TRUE;
}

// Destructor
CMainDlg::~CMainDlg()
{
    if (W32_CloseDevice(m_hDevice, 0))
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
int   msgColWidth[LIST_COLS] = { 30, 60, 229000 };

const RESIZE_INFO resizeInfo[] =
{
	{	IDC_OPEN_CLOSE, RESIZE_X },
	{	IDC_SEND, RESIZE_X },
	{	IDC_LISTVIEW, RESIZE_W | RESIZE_H }
};

BOOL CMainDlg::OnInitDialog(WPARAM wParam, LPARAM lParam)
{
	m_iResizeCount = COUNT_OF(resizeInfo);
	m_pResizeInfo = resizeInfo;
    // Set icons
    // Attach icon to main dialog
    HICON hIcon = LoadIcon(m_hInst, MAKEINTRESOURCE (IDD_MAIN));
    SendMessage(m_hWnd, WM_SETICON, TRUE, (LPARAM) hIcon);
    SendMessage (m_hWnd, WM_SETICON, FALSE, (LPARAM) hIcon);

    m_hListView = GetDlgItem(m_hWnd, IDC_LISTVIEW);
    m_hComboPort = GetDlgItem(m_hWnd, IDC_PORT);

    GetModuleFileName(NULL, szProfile, MAX_PATH);
    int len = lstrlen(szProfile);
    if (szProfile[len - 4] == '.')
    {
        lstrcpy(szProfile + len - 3, "ini");
    }

    g_uTimeOut = GetPrivateProfileInt(szSettings, szTimeOut, DEFAULT_TIMEOUT, szProfile);
    m_uHeaderBytes = GetPrivateProfileInt(szSettings, szHeaderBytes, DEFAULT_HEADER, szProfile);

    // Fill in USB device names
    if (QueryCOMPorts(m_hComboPort) > 0)
    {
        ComboBox_SetCurSel(m_hComboPort, 0);
    }

	ReadBinaryFile("FlatField.bin");

    UpdateUI();

    return TRUE;
}

void CMainDlg::EnableControls(BOOL flag)
{
    HWND hWnd= GetDlgItem(m_hWnd, IDC_SEND);
    EnableWindow(hWnd, !flag);
    EnableWindow(m_hComboPort, flag);
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

long BenchmarkRead(FT_HANDLE hDevice, LPBYTE rxbuf, size_t count, DWORD timeout)
{
    DWORD dwStart = GET_TIME_MSEC();
    do
    {
        size_t chunk = (count < BUF_SIZE) ? count : BUF_SIZE;
        size_t read = W32_ReadBytes(hDevice, rxbuf, chunk, timeout);
        if (read != chunk)
            return -1L;

        count -= read;
    }
    while (count > 0);
    return GET_TIME_MSEC() - dwStart;
}

const BYTE txbuf[] = { 0x00, 0x01, 0x2E, 0x0D };

size_t CMainDlg::CommandResponse(LPBYTE txbuf, size_t count, LPBYTE rxbuf, size_t length, DWORD timeout)
{
    size_t written;
	size_t read = 0;
    written = W32_WriteBytes(m_hDevice, txbuf, count);
    if (m_bLog && written > 0)
    {
        Log_AddItem(m_hListView, txbuf, written | LOG_SENT_FLAG);
    }

    size_t chunk = m_uHeaderBytes;
    if (chunk >= BUF_SIZE)
    {
        // 21-Dec-2016: added to measure read throughput on USB CDC device
        TCHAR szText[STR_MAX];
        long dwElapsed = BenchmarkRead(m_hDevice, rxbuf, chunk, timeout);
        wsprintf(szText, "Read %u bytes=%d.%03u sec", chunk, dwElapsed / 1000, dwElapsed % 1000);
        if (m_bLog)
        {
            MessageBox(m_hWnd, szText, "Benchmark", MB_ICONINFORMATION);
        }
    }
    else
    {
        read = W32_ReadBytes(m_hDevice, rxbuf, m_uHeaderBytes, timeout);
        if (m_bLog && read > 0)
        {
            Log_AddItem(m_hListView, rxbuf, read);
        }
    }
	return read;
}

ULONG __stdcall SendThread(LPVOID pThis)
{
    ((CMainDlg*) pThis)->DoSend();
    return 0;
}

void CMainDlg::DoSend(void)
{
	BOOL bCapture = IsDlgButtonChecked(m_hWnd, IDC_CAPTURE);
	UINT uRepeat = 1000;
	UINT loop;
    const DWORD dwStart = GET_TIME_MSEC();
    for (loop = 0; loop < uRepeat && m_hDevice != INVALID_HANDLE_VALUE; ++loop)
    {
        size_t read = CommandResponse((LPBYTE) txbuf, COUNT_OF(txbuf), m_rxbuf, BUF_SIZE, 0);
		if (bCapture) {
			TCHAR szFilename[MAX_PATH];
			wsprintf(szFilename, "Data%04u.bin", loop);
			FILE* fp = fopen(szFilename, "wb");
			if (fp != NULL) {
				fwrite(m_rxbuf, read, 1, fp);
				fclose(fp);
			}
		}
		HWND hButton = GetDlgItem(m_hWnd, IDC_LISTVIEW);
		if (hButton)
		{
			InvalidateRect(hButton, NULL, FALSE);
		}
    }
    if (uRepeat > 1)
    {
        TCHAR szText[STR_MAX];
        DWORD dwElapsed = GET_TIME_MSEC() - dwStart;
        wsprintf(szText, "%u Frames in %u.%03u sec", loop, dwElapsed / 1000, dwElapsed % 1000);
        MessageBox(m_hWnd, szText, "Info", MB_ICONINFORMATION);
    }
}

void CMainDlg::ReadBinaryFile(LPCTSTR pszFilename)
{
    HANDLE hFile = CreateFile(pszFilename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                       /* security=*/ NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	m_dwDataSize = 0;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD dwSize = GetFileSize(hFile, NULL);
        m_pDataFile = (LPBYTE) GlobalAllocPtr(GMEM_SHARE, dwSize);
        if (m_pDataFile != NULL)
        {
            DWORD dwRead = 0;
			::ReadFile(hFile, m_pDataFile, dwSize, &m_dwDataSize, NULL);
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

#define DT_FLAGS    (DT_END_ELLIPSIS | DT_VCENTER | DT_SINGLELINE)
#define LIGHT_RED   RGB(255,224,224)
#define LIGHT_GRN   RGB(224,255,224)

#define LINE_MAX	16384

BOOL CMainDlg::DrawView(LPDRAWITEMSTRUCT lpDIS)
{
    int iItem = lpDIS->itemID;
    LIST_ITEM* pLog = &g_log_buf[iItem % MAX_COUNT];
    HDC hDC = lpDIS->hDC;
	BMP_FILE bmpFile;
	DWORD pixels = m_dwDataSize / sizeof(uint16_t);

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
    
    // Flat Field image
	if (pixels && m_pDataFile != NULL) {
		memcpy(m_image, m_rxbuf, pixels * sizeof(uint16_t));
		ffc_offset((uint16_t*) m_image, (uint16_t*) m_pDataFile, pixels);

		uint8_t* pixmap = raw2bmp((uint16_t*) m_image, pixels, LANDSCAPE, &bmpFile);
		if (pixmap)
		{
			int width = lpDIS->rcItem.right - lpDIS->rcItem.left;
			int height = lpDIS->rcItem.bottom - lpDIS->rcItem.top;
			int x = lpDIS->rcItem.left;
			int y = lpDIS->rcItem.top;
			::StretchDIBits(hDC, x, y, width, height, 0, 0, bmpFile.bmiHeader.biWidth, bmpFile.bmiHeader.biHeight,
				pixmap, (BITMAPINFO*) &bmpFile.bmiHeader, DIB_RGB_COLORS, SRCCOPY);
			free(pixmap);
		}
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
    case IDC_LISTVIEW:
        DrawView(lpDIS);
        break;
    default:
        MsgBox(MB_ICONERROR, IDS_ERR_DRAWITEM, wId);
    }
    return TRUE;
}

BOOL CMainDlg::OnCommand(WPARAM wId)
{
    WORD code = HIWORD(wId);
    WORD id = LOWORD(wId);
    TRACE2("OnCommand(code=%04x,id=%u\n", code, id);

    switch (id)
    {
    case IDC_OPEN_CLOSE:
        if (m_hDevice != INVALID_HANDLE_VALUE)
        {
            if (W32_CloseDevice(m_hDevice, 0))
            {
                m_hDevice = INVALID_HANDLE_VALUE;
            }
        }
        else
        {
            TCHAR szPort[STR_MAX];
            if (GetDlgItemText(m_hWnd, IDC_PORT, szPort, STR_MAX))
            {
                m_hDevice = W32_OpenDevice(szPort, 0);
				if (m_hDevice != INVALID_HANDLE_VALUE)
				{
					OnCommand(IDC_SEND);
				}
            }
        }
        UpdateUI();
        break;
    case IDC_SEND:
        // Start separate thread to transmit/receive messages
        CreateThread(NULL, /*stacksize=*/ 1000000, SendThread, this, 0/*run after creation*/, &m_dwThreadId);
        break;
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
