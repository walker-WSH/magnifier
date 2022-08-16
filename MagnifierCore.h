#pragma once
#include "MagnifierCapture.h"
#include <map>

class MagnifierCore {
public:
	~MagnifierCore();

	static HRESULT STDMETHODCALLTYPE PresentEx_Callback(IDirect3DDevice9Ex *device, CONST RECT *src_rect, CONST RECT *dst_rect, HWND override_window, CONST RGNDATA *dirty_region, DWORD flags);
	static HRESULT STDMETHODCALLTYPE Reset_Callback(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params);
	static HRESULT STDMETHODCALLTYPE ResetEx_Callback(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *dmex);
	static MagnifierCore *Instance();

	bool Init();
	void Uninit();

	std::shared_ptr<MagnifierCapture> CreateMagnifier();
	void DestroyMagnifier(std::shared_ptr<MagnifierCapture> &);

protected:
	MagnifierCore();

	void ClearMagnifier();
	std::shared_ptr<MagnifierCapture> FindMagnifier(DWORD tid);

	bool RegisterTestClass();
	bool InitFuncAddr();
	bool HookFunc();
	void UnHookFunc();

private:
	bool m_bInited = false;

	HMODULE m_hModule = 0; // need to free
	PresentEx_t m_pRealPresentEx = nullptr;
	Reset_t m_pRealReset = nullptr;
	ResetEx_t m_pRealResetEx = nullptr;
	bool m_bResetHooked = false;

	std::recursive_mutex m_lockList;
	std::map<DWORD, std::shared_ptr<MagnifierCapture>> m_mapMagList; // need to free
};
