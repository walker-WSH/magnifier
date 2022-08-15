#pragma once
#include <vector>
#include <mutex>
#include <memory>
#include <windows.h>
#include <process.h>
#include <wincodec.h>
#include <magnification.h>
#include <functional>
#include <assert.h>

#define DEBUG_MAG_WINDOW 0

class MagnifierCapture : public std::enable_shared_from_this<MagnifierCapture> {
public:
	MagnifierCapture();
	~MagnifierCapture();

	void Start();
	void Stop();

	void SetExcludeWindow(std::vector<HWND> filter);
	void SetCaptureRegion(RECT rcScreen);

private:
	static LRESULT __stdcall HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static unsigned __stdcall MagnifierThread(void *pParam);
	bool RegisterMagClass();
	void MagnifierThreadInner();
	BOOL SetupMagnifier(HINSTANCE hInst);
	void CaptureVideo();
	void PushTask(std::function<void()> func);
	void RunTask();

private:
	HANDLE m_hExitEvent = 0;
	HANDLE m_hMagThread = 0;

	std::recursive_mutex m_lockList;
	std::vector<std::function<void()>> m_TaskList;

	// Access them in magnifier thread
	std::vector<HWND> m_vIgnore;
	RECT m_rcCaptureScreen = {0};

	HWND m_hHostWindow = 0;
	HWND m_hMagChild = 0;
};
