#ifndef __APP_DIALOG_H__
#define __APP_DIALOG_H__

#define STR_MAX		80

#define RESIZE_NONE								0
#define RESIZE_X								8
#define RESIZE_Y								4
#define RESIZE_W								2
#define RESIZE_H								1

// Definitions copied from dlgfont.c
// Source: http://github.com/strobejb/HexEdit/blob/master/src/HexEdit/dlgfont.c

#pragma pack(push, 1)

typedef struct 
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

typedef struct
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

typedef struct 
{
  WORD     pointsize; 
  WORD     weight; 
  BYTE     italic;
  BYTE     charset; 
  WCHAR    typeface[1];  

} DLGTEMPLATEEXFONT; 


class CAppDialog
{
protected:
    HINSTANCE m_hInst;
public:
    CAppDialog(HINSTANCE hInst);

	SIZE m_InitialSize;
	SIZE m_oldSize;
	DLGTEMPLATEEX* m_pDlgTemplateEx;
	DLGITEMTEMPLATEEX* m_pDlgItemsEx;

    HWND m_hWnd;
    TCHAR m_szAppName[STR_MAX];

    BOOL DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

	virtual BOOL OnGetMinMaxInfo(LPMINMAXINFO pInfo);
	virtual BOOL OnSize(WPARAM wParam, LPARAM lParam);
    virtual BOOL OnInitDialog(WPARAM wParam, LPARAM lParam);
    virtual BOOL OnDrawItem(WPARAM wParam, LPDRAWITEMSTRUCT lParam);
    virtual BOOL OnCommand(WPARAM wId);
    virtual BOOL OnNotify(WPARAM wId, LPNMHDR nmhdr);

    BOOL CreateDialogBox(LPCTSTR pszResource, HWND hParent);

    void MsgBox(UINT type, UINT id, ...);
    void OnClose();
    void SleepYield(DWORD msec);
	BOOL ShowWindow(int nCmdShow);
	int Run();

protected:
    HWND m_hListView;
    HWND m_hCombo;
	HWND m_hProgress;

	void ShowProgress(UINT done, UINT total);
    BOOL SendBytes(LPVOID pBuffer, UINT count);
	void DoSend(void);
	BOOL DoLoopback(void);
    void FillCombo(LPCTSTR pszFilename);
    void ClearLog(void);
    void GetCellRect(int col, LPCRECT pItemRect, LPRECT pCellRect);
    BOOL DrawListView(LPDRAWITEMSTRUCT lpDIS);
};

#endif // __APP_DIALOG_H__
