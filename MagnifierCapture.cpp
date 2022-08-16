#include "pch.h"
#include "MagnifierCapture.h"
#include "MagnifierCore.h"

#pragma comment(lib, "Magnification.lib")

#define MAG_WINDOW_CLASS TEXT("MagnifierWindow")
#define MSG_MAG_TASK WM_USER + 1
#define MAG_TICK_TIMER_ID 1000
#define MAG_TICK_INTERVAL 50 // in ms

unsigned __stdcall MagnifierCapture::MagnifierThread(void *pParam)
{
	MagnifierCapture *self = reinterpret_cast<MagnifierCapture *>(pParam);
	self->MagnifierThreadInner();
	self->RunTask();
	return 0;
}

LRESULT __stdcall MagnifierCapture::HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	MagnifierCapture *self = (MagnifierCapture *)::GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (message)
	case WM_CREATE: {
		SetTimer(hWnd, MAG_TICK_TIMER_ID, MAG_TICK_INTERVAL, NULL);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_TIMER:
		if (self && wParam == MAG_TICK_TIMER_ID) {
			self->CaptureVideo();
			return 0;
		}
		break;

	case MSG_MAG_TASK:
	default:
		if (self)
			self->RunTask();

		break;
	}

		return DefWindowProc(hWnd, message, wParam, lParam);
}

MagnifierCapture::MagnifierCapture()
{
	RegisterMagClass();
}

MagnifierCapture::~MagnifierCapture()
{
	assert(!IsWindow(m_hHostWindow));
	UnregisterClass(MAG_WINDOW_CLASS, GetModuleHandle(0));
}

void MagnifierCapture::SetCaptureRegion(RECT rcScreen)
{
	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);
	if (!self)
		return;

	PushTask([self, rcScreen]() { self->m_rcCaptureScreen = rcScreen; });
}

void MagnifierCapture::SetExcludeWindow(std::vector<HWND> filter)
{
	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);
	if (!self)
		return;

	PushTask([self, filter]() {
		if (!filter.empty())
			MagSetWindowFilterList(self->m_hMagChild, MW_FILTERMODE_EXCLUDE, (int)filter.size(), (HWND *)filter.data());
	});
}

DWORD MagnifierCapture::Start()
{
	if (m_hMagThread && m_hMagThread != INVALID_HANDLE_VALUE) {
		assert(false);
		return 0;
	}

	m_hMagThread = (HANDLE)_beginthreadex(0, 0, MagnifierThread, this, 0, 0);

	if (m_hMagThread && m_hMagThread != INVALID_HANDLE_VALUE) {
		m_dwThreadID = GetThreadId(m_hMagThread);
		return m_dwThreadID;
	} else {
		assert(false);
		return 0;
	}
}

void MagnifierCapture::Stop()
{
	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);

	bool bMagRunning = (m_hMagThread && m_hMagThread != INVALID_HANDLE_VALUE);
	if (!bMagRunning)
		return;

	PushTask([self, this]() {
		if (IsWindow(m_hHostWindow))
			DestroyWindow(m_hHostWindow);

		m_hHostWindow = 0;
		m_hMagChild = 0;
	});

	WaitForSingleObject(m_hMagThread, INFINITE);
	CloseHandle(m_hMagThread);
	m_hMagThread = 0;
}

bool MagnifierCapture::RegisterMagClass()
{
	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = HostWndProc;
	wcex.hInstance = GetModuleHandle(0);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wcex.lpszClassName = MAG_WINDOW_CLASS;

	if (0 == RegisterClassEx(&wcex) && ERROR_CLASS_ALREADY_EXISTS != GetLastError()) {
		assert(false);
		return false;
	}
	return true;
}

void MagnifierCapture::MagnifierThreadInner()
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

	if (!MagInitialize()) {
		assert(false);
		return;
	}

	if (!SetupMagnifier(GetModuleHandle(0))) {
		assert(false);
		return;
	}

	ShowWindow(m_hHostWindow, SW_SHOW);
	UpdateWindow(m_hHostWindow);

	RunTask();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	MagUninitialize();
}

bool MagnifierCapture::SetupMagnifier(HINSTANCE hInst)
{
	DWORD dwStyle = WS_POPUP | WS_CLIPCHILDREN;
	DWORD dwExStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW;
	m_hHostWindow = CreateWindowEx(dwExStyle, MAG_WINDOW_CLASS, TEXT("NAVER Magnifier"), dwStyle, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
	if (!m_hHostWindow)
		return false;

	SetWindowLongPtr(m_hHostWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	if (DEBUG_MAG_WINDOW)
		SetLayeredWindowAttributes(m_hHostWindow, 0, 255, LWA_ALPHA);
	else
		SetLayeredWindowAttributes(m_hHostWindow, 0, 0, LWA_ALPHA);

	RECT rc;
	GetClientRect(m_hHostWindow, &rc);
	m_hMagChild = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"), WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hHostWindow, NULL,
				   hInst, NULL);
	if (!m_hMagChild) {
		DestroyWindow(m_hHostWindow);
		m_hHostWindow = 0;
		return false;
	}

	if (DEBUG_MAG_WINDOW) {
		MAGCOLOREFFECT magEffectInvert = {{// MagEffectInvert
						   {-1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
						   {0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
						   {0.0f, 0.0f, -1.0f, 0.0f, 0.0f},
						   {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
						   {1.0f, 1.0f, 1.0f, 0.0f, 1.0f}}};

		MagSetColorEffect(m_hMagChild, &magEffectInvert);
	}

	return true;
}

void MagnifierCapture::CaptureVideo()
{
	LONG cx = m_rcCaptureScreen.right - m_rcCaptureScreen.left;
	LONG cy = m_rcCaptureScreen.bottom - m_rcCaptureScreen.top;
	if (!cx || !cy)
		return;

	SetWindowPos(m_hHostWindow, NULL, m_rcCaptureScreen.left, m_rcCaptureScreen.top, cx, cy, 0);

	RECT rc;
	GetClientRect(m_hHostWindow, &rc);
	SetWindowPos(m_hMagChild, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0);

	MagSetWindowSource(m_hMagChild, m_rcCaptureScreen);
}

void MagnifierCapture::PushTask(std::function<void()> func)
{
	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockTask);
		m_vTaskList.push_back(func);
	}

	if (IsWindow(m_hHostWindow))
		PostMessage(m_hHostWindow, MSG_MAG_TASK, 0, 0);
}

void MagnifierCapture::RunTask()
{
	std::vector<std::function<void()>> tasks;

	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockTask);
		tasks.swap(m_vTaskList);
	}

	for (auto &item : tasks)
		item();
}

static inline HRESULT get_backbuffer(IDirect3DDevice9 *device, IDirect3DSurface9 **surface)
{
	static bool use_backbuffer = false;
	static bool checked_exceptions = false;

	//if (!checked_exceptions) {
	//	if (_strcmpi(get_process_name(), "hotd_ng.exe") == 0)
	//		use_backbuffer = true;
	//	checked_exceptions = true;
	//}

	if (use_backbuffer) {
		return device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, surface);
	} else {
		return device->GetRenderTarget(0, surface);
	}
}

bool MagnifierCapture::OnPresentEx(IDirect3DDevice9Ex *device)
{
	assert(GetCurrentThreadId() == m_dwThreadID);

	if (m_pDeviceEx != device)
		FreeDX();

	if (!m_pDeviceEx)
		InitDX9(device);

	if (!m_pDeviceEx)
		return false; // never inited

	return CaptureDX9();
}

void MagnifierCapture::FreeDX()
{
	assert(GetCurrentThreadId() == m_dwThreadID);
	m_pDeviceEx = nullptr;
	m_pSurface = nullptr;
	m_D3DFormat = D3DFMT_UNKNOWN;
	m_uWidth = 0;
	m_uHeight = 0;
	m_nSurfacePitch = 0;
}

bool MagnifierCapture::InitTextureInfo(IDirect3DDevice9Ex *device)
{
	ComPtr<IDirect3DSwapChain9> swap;
	HRESULT hr = device->GetSwapChain(0, &swap);
	if (FAILED(hr))
		return false;

	D3DPRESENT_PARAMETERS pp;
	hr = swap->GetPresentParameters(&pp);
	if (FAILED(hr))
		return false;

	m_D3DFormat = pp.BackBufferFormat;
	m_uWidth = pp.BackBufferWidth;
	m_uHeight = pp.BackBufferHeight;

	ComPtr<IDirect3DSurface9> bkBuffer;
	hr = device->GetRenderTarget(0, &bkBuffer);
	if (SUCCEEDED(hr)) {
		D3DSURFACE_DESC desc;
		hr = bkBuffer->GetDesc(&desc);
		if (SUCCEEDED(hr)) {
			m_D3DFormat = desc.Format;
			m_uWidth = desc.Width;
			m_uHeight = desc.Height;
		}
	}

	return (m_D3DFormat == D3DFMT_A8R8G8B8); // DXGI_FORMAT_B8G8R8A8_UNORM
}

bool MagnifierCapture::CreateCopySurface(IDirect3DDevice9Ex *device)
{
	HRESULT hr = device->CreateOffscreenPlainSurface(m_uWidth, m_uHeight, m_D3DFormat, D3DPOOL_SYSTEMMEM, &m_pSurface, nullptr);
	if (FAILED(hr))
		return false;

	D3DLOCKED_RECT rect;
	hr = m_pSurface->LockRect(&rect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
		return false;

	m_nSurfacePitch = rect.Pitch;
	m_pSurface->UnlockRect();

	return true;
}

bool MagnifierCapture::InitDX9(IDirect3DDevice9Ex *device)
{
	if (!InitTextureInfo(device)) {
		FreeDX();
		return false;
	}

	if (!CreateCopySurface(device)) {
		FreeDX();
		return false;
	}

	m_pDeviceEx = device;
	return true;
}

bool MagnifierCapture::CaptureDX9()
{
	assert(m_pDeviceEx);

	ComPtr<IDirect3DSurface9> backbuffer;
	HRESULT hr = m_pDeviceEx->GetRenderTarget(0, backbuffer.Assign());
	if (FAILED(hr))
		return false;

	hr = m_pDeviceEx->GetRenderTargetData(backbuffer, m_pSurface);
	if (FAILED(hr))
		return false;

	D3DLOCKED_RECT rect;
	hr = m_pSurface->LockRect(&rect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
		return false;

	//save_as_bitmap_file("d:\\test.bmp", (uint8_t *)rect.pBits, m_nSurfacePitch, m_uWidth, m_uHeight, 4, true);
	// TODO shmem_copy_data(i, rect.pBits);
	m_pSurface->UnlockRect();

	return true;
}
