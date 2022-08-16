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
#include <d3d9.h>
#include <detours.h>
#include <queue>
#include <atomic>
#include "ComPtr.hpp"

#define DEBUG_MAG_WINDOW 0

using Direct3DCreate9Ex_t = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
using PresentEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9Ex *, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD);
using Reset_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
using ResetEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);

struct ST_MagnifierFrame {
	const D3DFORMAT m_D3DFormat = D3DFMT_A8R8G8B8;
	UINT m_uWidth = 0;
	UINT m_uHeight = 0;
	INT m_nPitch = 0;
	std::shared_ptr<uint8_t> m_pVideoData = nullptr;
};

class MagnifierCapture : public std::enable_shared_from_this<MagnifierCapture> {
	friend class MagnifierCore;

public:
	~MagnifierCapture();

	void SetFPS(int fps);
	void SetExcludeWindow(std::vector<HWND> filter);
	void SetCaptureRegion(RECT rcScreen);

	// Second value: bool bCaptureNormalRunning
	std::pair<std::shared_ptr<ST_MagnifierFrame>, bool> PopVideo();

protected:
	static LRESULT __stdcall HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static unsigned __stdcall MagnifierThread(void *pParam);

	MagnifierCapture();
	bool RegisterMagClass();

	DWORD Start();
	void Stop();

	void MagnifierThreadInner();
	bool SetupMagnifier(HINSTANCE hInst);
	void CaptureVideo();

	void PushTask(std::function<void()> func);
	void RunTask();

	bool OnPresentEx(IDirect3DDevice9Ex *device);
	void FreeDX();
	void CheckFree(IDirect3DDevice9Ex *device);
	bool InitDX9(IDirect3DDevice9Ex *device);
	bool CaptureDX9();
	bool InitTextureInfo(IDirect3DDevice9Ex *device);
	bool CreateCopySurface(IDirect3DDevice9Ex *device);

	void PushVideo(D3DLOCKED_RECT &rect);
	void ClearVideo();
	std::shared_ptr<uint8_t> GetIdleFrame();

private:
	std::recursive_mutex m_lockTask;
	std::vector<std::function<void()>> m_vTaskList;

	std::recursive_mutex m_lockFrame;
	std::vector<std::shared_ptr<ST_MagnifierFrame>> m_vFrameList;
	std::atomic<ULONGLONG> m_dwPreCaptureTime = 0;

	// Accessed in magnifier thread
	RECT m_rcCaptureScreen = {0};
	std::queue<std::shared_ptr<uint8_t>> m_IdleList;

	DWORD m_dwThreadID = 0;
	HANDLE m_hMagThread = 0;
	HWND m_hHostWindow = 0;
	HWND m_hMagChild = 0;

	IDirect3DDevice9Ex *m_pDeviceEx = nullptr; /* do not release */
	ComPtr<IDirect3DSurface9> m_pSurface = nullptr;
	D3DFORMAT m_D3DFormat = D3DFMT_UNKNOWN;
	UINT m_uWidth = 0;
	UINT m_uHeight = 0;
	INT m_nPitch = 0;
};
