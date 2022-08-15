#include "pch.h"
#include "MagnifierCore.h"
#include <detours.h>

#define DX9_WINDOW_CLASS TEXT("TestDX9ClassName")

HRESULT STDMETHODCALLTYPE MagnifierCore ::PresentEx_Callback(IDirect3DDevice9Ex *device, CONST RECT *src_rect, CONST RECT *dst_rect, HWND override_window, CONST RGNDATA *dirty_region, DWORD flags)
{
	// TODO

	OutputDebugStringA("PresentEx_Callback ================ \n");

	IDirect3DSurface9 *backbuffer = nullptr;

	//if (!hooked_reset)
	//	setup_reset_hooks(device);

	//present_begin(device, backbuffer);

	//const HRESULT hr = RealPresentEx(device, src_rect, dst_rect, override_window, dirty_region, flags);

	//present_end(device, backbuffer);

	//return hr;
	return S_OK;
}

//static HRESULT STDMETHODCALLTYPE hook_reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params)
//{
//	if (capture_active())
//		d3d9_free();
//
//	return RealReset(device, params);
//}
//
//static HRESULT STDMETHODCALLTYPE hook_reset_ex(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *dmex)
//{
//	if (capture_active())
//		d3d9_free();
//
//	return RealResetEx(device, params, dmex);
//}

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
	assert(m_vMagList.empty());
	m_vMagList.clear();
	if (m_hModule)
		FreeLibrary(m_hModule);
}

bool MagnifierCore::Init()
{
	if (m_bInited)
		return false;

	RegisterMagClass();

	RealPresentEx = (PresentEx_t)GetPresentExAddr();
	if (!RealPresentEx) {
		assert(false);
		return false;
	}

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID *)&RealPresentEx, PresentEx_Callback);
	const LONG error = DetourTransactionCommit();
	const bool success = (error == NO_ERROR);
	if (!success) {
		assert(false);
		return false;
	}

	m_bInited = true;
	return true;
}

void MagnifierCore::Uninit()
{
	assert(m_vMagList.empty());
	m_vMagList.clear();

	if (!m_bInited)
		return;

	if (RealPresentEx) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach((PVOID *)&RealPresentEx, PresentEx_Callback);
		DetourTransactionCommit();
	}

	UnregisterClass(DX9_WINDOW_CLASS, GetModuleHandle(0));
	m_bInited = false;
}

std::shared_ptr<MagnifierCapture> MagnifierCore::CreateMagnifier()
{
	auto ret = std::shared_ptr<MagnifierCapture>(new MagnifierCapture());

	std::lock_guard<std::recursive_mutex> autoLock(m_lockList);
	m_vMagList.push_back(ret);

	return ret;
}

void MagnifierCore::DestroyMagnifier(std::shared_ptr<MagnifierCapture> ptr)
{
	std::lock_guard<std::recursive_mutex> autoLock(m_lockList);
	auto itr = m_vMagList.begin();
	while (itr != m_vMagList.end()) {
		if (itr->get() == ptr.get()) {
			m_vMagList.erase(itr);
			return;
		}
	}
}

bool MagnifierCore ::RegisterMagClass()
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

void *MagnifierCore ::GetPresentExAddr()
{
	HWND hWnd = 0;
	IDirect3D9Ex *d3d9ex = nullptr;
	IDirect3DDevice9Ex *d3d9Device = nullptr;

	{ // TODO
		if (d3d9Device)
			d3d9Device->Release();
		if (d3d9ex)
			d3d9ex->Release();
		if (hWnd)
			DestroyWindow(hWnd);
	}

	if (!m_hModule)
		return nullptr;

	hWnd = CreateWindowEx(0, DX9_WINDOW_CLASS, TEXT("d3d9 offset"), WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
	if (!hWnd)
		return nullptr;

	Direct3DCreate9Ex_t create = (Direct3DCreate9Ex_t)GetProcAddress(m_hModule, "Direct3DCreate9Ex");
	if (!create)
		return nullptr;

	HRESULT hr = create(D3D_SDK_VERSION, &d3d9ex);
	if (FAILED(hr))
		return nullptr;

	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = true;
	pp.SwapEffect = D3DSWAPEFFECT_FLIP;
	pp.BackBufferFormat = D3DFMT_A8R8G8B8;
	pp.BackBufferWidth = 2;
	pp.BackBufferHeight = 2;
	pp.BackBufferCount = 1;
	pp.hDeviceWindow = hWnd;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &pp, nullptr, &d3d9Device);
	if (FAILED(hr))
		return nullptr;

	uintptr_t *pVirtualFuncList = *(uintptr_t **)d3d9Device;
	return (void *)pVirtualFuncList[121]; // PresentEx
}
