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
	friend class MagnifierCore;

public:
	~MagnifierCapture();

	void SetExcludeWindow(std::vector<HWND> filter);
	void SetCaptureRegion(RECT rcScreen);

protected:
	static LRESULT __stdcall HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static unsigned __stdcall MagnifierThread(void *pParam);
	
	MagnifierCapture();

	DWORD Start();
	void Stop();

	bool RegisterMagClass();
	void MagnifierThreadInner();
	bool SetupMagnifier(HINSTANCE hInst);
	void CaptureVideo();
	void PushTask(std::function<void()> func);
	void RunTask();

private:
	std::recursive_mutex m_lockList;
	std::vector<std::function<void()>> m_vTaskList;

	// Accessed in magnifier thread
	RECT m_rcCaptureScreen = {0};

	DWORD m_dwThreadID = 0;
	HANDLE m_hMagThread = 0;
	HWND m_hHostWindow = 0;
	HWND m_hMagChild = 0;
};
