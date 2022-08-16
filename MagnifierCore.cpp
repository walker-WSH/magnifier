#include "pch.h"
#include "MagnifierCore.h"
#include "ComPtr.hpp"

#define DX9_WINDOW_CLASS TEXT("DX9TestClassName")

HRESULT STDMETHODCALLTYPE MagnifierCore ::PresentEx_Callback(IDirect3DDevice9Ex *device, CONST RECT *src_rect, CONST RECT *dst_rect, HWND override_window, CONST RGNDATA *dirty_region, DWORD flags)
{
#ifdef DEBUG
	char buf[MAX_PATH];
	snprintf(buf, MAX_PATH, "callback of PresentEx, TID: %u , Time: %u \n", GetCurrentThreadId(), GetTickCount());
	OutputDebugStringA(buf);
#endif

	std::shared_ptr<MagnifierCapture> mag = MagnifierCore::Instance()->FindMagnifier(GetCurrentThreadId());
	if (mag)
		mag->OnPresentEx(device, src_rect, dst_rect, override_window, dirty_region, flags);

	return MagnifierCore::Instance()->m_pRealPresentEx(device, src_rect, dst_rect, override_window, dirty_region, flags);
}

HRESULT STDMETHODCALLTYPE MagnifierCore::Reset_Callback(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params)
{
	std::shared_ptr<MagnifierCapture> mag = MagnifierCore::Instance()->FindMagnifier(GetCurrentThreadId());
	if (mag)
		mag->FreeDX();

	return MagnifierCore::Instance()->m_pRealReset(device, params);
}

HRESULT STDMETHODCALLTYPE MagnifierCore::ResetEx_Callback(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *dmex)
{
	std::shared_ptr<MagnifierCapture> mag = MagnifierCore::Instance()->FindMagnifier(GetCurrentThreadId());
	if (mag)
		mag->FreeDX();

	return MagnifierCore::Instance()->m_pRealResetEx(device, params, dmex);
}

MagnifierCore *MagnifierCore::Instance()
{
	static MagnifierCore ins;
	return &ins;
}

MagnifierCore ::MagnifierCore()
{
	m_hModule = LoadLibraryA("d3d9.dll");
	assert(m_hModule);
}

MagnifierCore ::~MagnifierCore()
{
	ClearMagnifier();
	if (m_hModule)
		FreeLibrary(m_hModule);
}

bool MagnifierCore::Init()
{
	if (m_bInited)
		return true;

	if (!InitFuncAddr()) {
		assert(false);
		return false;
	}

	if (!HookFunc()) {
		assert(false);
		return false;
	}

	m_bInited = true;
	return true;
}

void MagnifierCore::Uninit()
{
	ClearMagnifier();

	if (m_bInited) {
		UnHookFunc();
		m_bInited = false;
	}
}

std::shared_ptr<MagnifierCapture> MagnifierCore::CreateMagnifier()
{
	assert(m_bInited);
	if (!m_bInited)
		return nullptr;

	auto ret = std::shared_ptr<MagnifierCapture>(new MagnifierCapture());
	DWORD tid = ret->Start();

	std::lock_guard<std::recursive_mutex> autoLock(m_lockList);
	assert(m_mapMagList[tid] == nullptr);
	m_mapMagList[tid] = ret;

	return ret;
}

void MagnifierCore::DestroyMagnifier(std::shared_ptr<MagnifierCapture> &ptr)
{
	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockList);

		auto itr = m_mapMagList.find(ptr->m_dwThreadID);
		assert(itr != m_mapMagList.end());
		if (itr != m_mapMagList.end())
			m_mapMagList.erase(itr);
	}

	ptr->Stop();
	ptr = nullptr;
}

bool MagnifierCore ::RegisterTestClass()
{
	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = (WNDPROC)DefWindowProcA;
	wcex.hInstance = GetModuleHandle(0);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wcex.lpszClassName = DX9_WINDOW_CLASS;

	if (0 == RegisterClassEx(&wcex) && ERROR_CLASS_ALREADY_EXISTS != GetLastError()) {
		assert(false);
		return false;
	}
	return true;
}

bool MagnifierCore ::InitFuncAddr()
{
	if (!m_hModule)
		return false;

	RegisterTestClass();

	HWND hWnd = 0;
	ComPtr<IDirect3D9Ex> d3d9ex;
	ComPtr<IDirect3DDevice9Ex> d3d9DeviceEx;

	RUN_WHEN_SECTION_END([=]() {
		if (hWnd)
			DestroyWindow(hWnd);

		UnregisterClass(DX9_WINDOW_CLASS, GetModuleHandle(0));
	});

	hWnd = CreateWindowEx(0, DX9_WINDOW_CLASS, TEXT("d3d9 offset"), WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	if (!hWnd)
		return false;

	Direct3DCreate9Ex_t create = (Direct3DCreate9Ex_t)GetProcAddress(m_hModule, "Direct3DCreate9Ex");
	if (!create)
		return false;

	HRESULT hr = create(D3D_SDK_VERSION, d3d9ex.Assign());
	if (FAILED(hr))
		return false;

	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = true;
	pp.SwapEffect = D3DSWAPEFFECT_FLIP;
	pp.BackBufferFormat = D3DFMT_A8R8G8B8;
	pp.BackBufferWidth = 2;
	pp.BackBufferHeight = 2;
	pp.BackBufferCount = 1;
	pp.hDeviceWindow = hWnd;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &pp, nullptr, d3d9DeviceEx.Assign());
	if (FAILED(hr))
		return false;

	uintptr_t *pVirtualFuncList = *(uintptr_t **)d3d9DeviceEx.Get();
	m_pRealPresentEx = (PresentEx_t)pVirtualFuncList[121]; // PresentEx

	IDirect3DDevice9 *d3d9Device = (IDirect3DDevice9 *)d3d9DeviceEx;
	uintptr_t *pVirtualFuncListBase = *(uintptr_t **)d3d9Device;
	m_pRealReset = (Reset_t)pVirtualFuncListBase[16];
	m_pRealResetEx = (ResetEx_t)pVirtualFuncListBase[132];

	return true;
}

void MagnifierCore ::ClearMagnifier()
{
	assert(m_mapMagList.empty());

	std::lock_guard<std::recursive_mutex> autoLock(m_lockList);

	for (auto &item : m_mapMagList) {
		item.second->Stop();
		item.second = nullptr;
	}

	m_mapMagList.clear();
}

std::shared_ptr<MagnifierCapture> MagnifierCore ::FindMagnifier(DWORD tid)
{
	std::lock_guard<std::recursive_mutex> autoLock(m_lockList);

	auto itr = m_mapMagList.find(tid);
	if (itr != m_mapMagList.end())
		return itr->second;
	else
		return nullptr;
}

bool MagnifierCore::HookFunc()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach((PVOID *)&m_pRealPresentEx, &MagnifierCore::PresentEx_Callback);
	DetourAttach((PVOID *)&m_pRealReset, &MagnifierCore::Reset_Callback);
	DetourAttach((PVOID *)&m_pRealResetEx, &MagnifierCore::ResetEx_Callback);

	const LONG error = DetourTransactionCommit();
	const bool success = (error == NO_ERROR);
	return success;
}

void MagnifierCore::UnHookFunc()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	if (m_pRealPresentEx)
		DetourDetach((PVOID *)&m_pRealPresentEx, &MagnifierCore::PresentEx_Callback);

	if (m_pRealReset)
		DetourAttach((PVOID *)&m_pRealReset, &MagnifierCore::Reset_Callback);

	if (m_pRealResetEx)
		DetourAttach((PVOID *)&m_pRealResetEx, &MagnifierCore::ResetEx_Callback);

	DetourTransactionCommit();
}
