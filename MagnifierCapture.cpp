#include "pch.h"
#include "MagnifierCapture.h"
#include "MagnifierCore.h"

#pragma comment(lib, "Magnification.lib")

#define MAG_WINDOW_CLASS TEXT("MagnifierWindow")
#define MSG_MAG_TASK WM_USER + 1
#define MAG_TICK_TIMER_ID 1000
#define MAX_IDLE_FRAME_COUNT 1
#define MAG_CAPTURE_ABORT 200 // in ms

unsigned __stdcall MagnifierCapture::MagnifierThread(void *pParam)
{
	MagnifierCapture *self = reinterpret_cast<MagnifierCapture *>(pParam);
	self->MagnifierThreadInner();
	self->RunTask();
	self->FreeDX();
	return 0;
}

LRESULT __stdcall MagnifierCapture::HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	MagnifierCapture *self = (MagnifierCapture *)::GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (message) {
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

void MagnifierCapture::SetFPS(int fps)
{
	assert(fps >= 10);

	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);
	if (!self)
		return;

	PushTask([self, fps]() {
		if (IsWindow(self->m_hHostWindow)) {
			KillTimer(self->m_hHostWindow, MAG_TICK_TIMER_ID);
			SetTimer(self->m_hHostWindow, MAG_TICK_TIMER_ID, 1000 / fps, NULL);
		}
	});
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

void MagnifierCapture::SetCaptureRegion(RECT rcScreen)
{
	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);
	if (!self)
		return;

	PushTask([self, rcScreen]() { self->m_rcCaptureScreen = rcScreen; });
}

std::pair<std::shared_ptr<ST_MagnifierFrame>, bool> MagnifierCapture::PopVideo()
{
	std::lock_guard<std::recursive_mutex> autoLock(m_lockFrame);

	ULONGLONG pre = m_dwPreCaptureTime;
	ULONGLONG crt = GetTickCount64();
	bool timeout = (crt > pre && (crt - pre) >= MAG_CAPTURE_ABORT);

	if (m_vFrameList.empty())
		return std::make_pair(nullptr, !timeout);

	auto ret = m_vFrameList.at(0);
	m_vFrameList.erase(m_vFrameList.begin());

	return std::make_pair(ret, true);
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
	if (GetCurrentThreadId() == m_dwThreadID) {
		func();
		return;
	}

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

bool MagnifierCapture::OnPresentEx(IDirect3DDevice9Ex *device)
{
	assert(GetCurrentThreadId() == m_dwThreadID);

	CheckFree(device);

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
	m_nPitch = 0;

	ClearVideo();
}

void MagnifierCapture::CheckFree(IDirect3DDevice9Ex *device)
{
	if (m_pDeviceEx != device) {
		FreeDX();
		return;
	}

	ComPtr<IDirect3DSurface9> bkBuffer;
	HRESULT hr = m_pDeviceEx->GetRenderTarget(0, bkBuffer.Assign());
	if (FAILED(hr))
		return;

	D3DSURFACE_DESC desc;
	hr = bkBuffer->GetDesc(&desc);
	if (FAILED(hr))
		return;

	if (desc.Format != m_D3DFormat || desc.Width != m_uWidth || desc.Height != m_uHeight) {
		FreeDX();
		return;
	}
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

	m_nPitch = rect.Pitch;
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

	ComPtr<IDirect3DSurface9> bkBuffer;
	HRESULT hr = m_pDeviceEx->GetRenderTarget(0, bkBuffer.Assign());
	if (FAILED(hr))
		return false;

	hr = m_pDeviceEx->GetRenderTargetData(bkBuffer, m_pSurface);
	if (FAILED(hr))
		return false;

	D3DLOCKED_RECT rect;
	hr = m_pSurface->LockRect(&rect, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr))
		return false;

	PushVideo(rect);
	m_pSurface->UnlockRect();

	return true;
}

void MagnifierCapture::PushVideo(D3DLOCKED_RECT &rect)
{
	assert(rect.Pitch == m_nPitch);
	assert(GetCurrentThreadId() == m_dwThreadID);

	size_t size = size_t(rect.Pitch) * size_t(m_uHeight);
	std::shared_ptr<uint8_t> data = GetIdleFrame();
	if (!data)
		data = std::shared_ptr<uint8_t>(new uint8_t[size]);

	memmove(data.get(), rect.pBits, size);

	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	std::weak_ptr<MagnifierCapture> wself(self);
	std::shared_ptr<ST_MagnifierFrame> vf(new ST_MagnifierFrame(), [wself](ST_MagnifierFrame *frame) {
		auto self = wself.lock();
		if (self) {
			ST_MagnifierFrame vf = *frame;
			self->PushTask([self, vf]() {
				if (vf.m_uWidth != self->m_uWidth || vf.m_uHeight != self->m_uHeight || vf.m_nPitch != self->m_nPitch)
					return;

				assert(GetCurrentThreadId() == self->m_dwThreadID);
				if (self->m_IdleList.size() < MAX_IDLE_FRAME_COUNT)
					self->m_IdleList.push(vf.m_pVideoData);
			});
		}

		delete frame;
	});

	vf->m_uWidth = m_uWidth;
	vf->m_uHeight = m_uHeight;
	vf->m_nPitch = rect.Pitch;
	vf->m_pVideoData = data;

	std::lock_guard<std::recursive_mutex> autoLock(m_lockFrame);
	m_vFrameList.clear();
	m_vFrameList.push_back(vf);
	m_dwPreCaptureTime = GetTickCount64();
}

void MagnifierCapture::ClearVideo()
{
	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockFrame);
		m_vFrameList.clear();
	}

	assert(GetCurrentThreadId() == m_dwThreadID);
	while (!m_IdleList.empty()) {
		m_IdleList.pop();
	}
}

std::shared_ptr<uint8_t> MagnifierCapture::GetIdleFrame()
{
	assert(GetCurrentThreadId() == m_dwThreadID);

	if (m_IdleList.empty())
		return nullptr;

	auto ret = m_IdleList.front();
	m_IdleList.pop();
	return ret;
}
