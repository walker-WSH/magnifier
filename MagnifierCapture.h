#pragma once
#include <vector>
#include <assert.h>
#include <windows.h>
#include <process.h>
#include <wincodec.h>
#include <magnification.h>

#define DEBUG_MAG_WINDOW 0

class MagnifierCapture {
public:
	MagnifierCapture();
	~MagnifierCapture();

	void Start(RECT rcScreen, std::vector<HWND> filter);
	void Stop();

private:
	static LRESULT __stdcall HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static unsigned __stdcall MagnifierThread(void *pParam);
	void MagnifierThreadInner();
	BOOL SetupMagnifier(HINSTANCE hInst);
	void UpdateMagWindow();
	bool RegisterMagClass();

private:
	HANDLE m_hExitEvent = 0;
	HANDLE m_hMagThread = 0;

	RECT m_rcCaptureScreen = {0};
	std::vector<HWND> m_vIgnore;

	HWND m_hHostWindow = 0;
	HWND m_hMagChild = 0;
};
