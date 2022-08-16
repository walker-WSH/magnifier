#pragma once
#include "MagnifierCapture.h"
#include <d3d9.h>

class CAutoRunWhenSecEnd {
public:
	CAutoRunWhenSecEnd(std::function<void()> f) : func(f) {}
	virtual ~CAutoRunWhenSecEnd() { func(); }

private:
	std::function<void()> func;
};
#define COMBINE2(a, b) a##b
#define COMBINE1(a, b) COMBINE2(a, b)
#define RUN_WHEN_SECTION_END(lambda) CAutoRunWhenSecEnd COMBINE1(autoRunVariable, __LINE__)(lambda)

using Direct3DCreate9Ex_t = HRESULT(WINAPI *)(UINT, IDirect3D9Ex **);
using PresentEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DSwapChain9 *, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD);
using Reset_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
using ResetEx_t = HRESULT(STDMETHODCALLTYPE *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);

class MagnifierCore {
public:
	~MagnifierCore();

	static HRESULT STDMETHODCALLTYPE PresentEx_Callback(IDirect3DDevice9Ex *device, CONST RECT *src_rect, CONST RECT *dst_rect, HWND override_window, CONST RGNDATA *dirty_region, DWORD flags);
	static MagnifierCore *Instance();

	bool Init();
	void Uninit();

	std::shared_ptr<MagnifierCapture> CreateMagnifier();
	void DestroyMagnifier(std::shared_ptr<MagnifierCapture> &);

protected:
	MagnifierCore();

	bool RegisterTestClass();
	void *GetPresentExAddr();

private:
	bool m_bInited = false;

	HMODULE m_hModule = 0; // need to free
	PresentEx_t m_pRealPresentEx = nullptr;
	Reset_t m_pRealReset = nullptr;
	ResetEx_t m_pRealResetEx = nullptr;

	std::recursive_mutex m_lockList;
	std::vector<std::shared_ptr<MagnifierCapture>> m_vMagList; // need to free
};
