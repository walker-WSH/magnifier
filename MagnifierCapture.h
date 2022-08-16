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
#include "ComPtr.hpp"

#define DEBUG_MAG_WINDOW 0

using Direct3DCreate9Ex_t = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
using PresentEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9Ex *, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD);
using Reset_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
using ResetEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);

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
	bool InitDX9(IDirect3DDevice9Ex *device);
	bool CaptureDX9();
	bool InitTextureInfo(IDirect3DDevice9Ex *device);
	bool CreateCopySurface(IDirect3DDevice9Ex *device);

private:
	std::recursive_mutex m_lockList;
	std::vector<std::function<void()>> m_vTaskList;

	// Accessed in magnifier thread
	RECT m_rcCaptureScreen = {0};

	DWORD m_dwThreadID = 0;
	HANDLE m_hMagThread = 0;
	HWND m_hHostWindow = 0;
	HWND m_hMagChild = 0;

	IDirect3DDevice9Ex *m_pDeviceEx = nullptr; /* do not release */
	ComPtr<IDirect3DSurface9> m_pSurface = nullptr;
	D3DFORMAT m_D3DFormat = D3DFMT_UNKNOWN;
	UINT m_uWidth = 0;
	UINT m_uHeight = 0;
	int m_nSurfacePitch = 0;
};
