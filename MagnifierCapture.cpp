#include "pch.h"
#include "MagnifierCapture.h"

#pragma comment(lib, "Magnification.lib")

#define MAG_WINDOW_CLASS TEXT("MagnifierWindow")

#define MSG_MAG_TASK WM_USER + 1

#define MAG_TICK_TIMER 1000
#define MAG_TICK_INTERVAL 40 // in ms

MagnifierCapture::MagnifierCapture()
{
	RegisterMagClass();
}

MagnifierCapture::~MagnifierCapture()
{
	assert(!IsWindow(m_hHostWindow));
	UnregisterClass(MAG_WINDOW_CLASS, GetModuleHandle(0));
}

void MagnifierCapture::Start()
{
	assert(m_hMagThread == 0);
	m_hMagThread = (HANDLE)_beginthreadex(0, 0, MagnifierThread, this, 0, 0);
}

void MagnifierCapture::Stop()
{
	std::shared_ptr<MagnifierCapture> self = shared_from_this();
	assert(self);

	PushTask([self]() {
		if (IsWindow(self->m_hHostWindow))
			DestroyWindow(self->m_hHostWindow);
	});

	if (m_hMagThread && m_hMagThread != INVALID_HANDLE_VALUE) {
		WaitForSingleObject(m_hMagThread, INFINITE);
		CloseHandle(m_hMagThread);
	}

	m_hMagThread = 0;
	m_hHostWindow = 0;
	m_hMagChild = 0;
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
	m_hHostWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, MAG_WINDOW_CLASS, TEXT("NAVER Magnifier"), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
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
	SetWindowPos(m_hHostWindow, NULL, m_rcCaptureScreen.left, m_rcCaptureScreen.top, m_rcCaptureScreen.right - m_rcCaptureScreen.left, m_rcCaptureScreen.bottom - m_rcCaptureScreen.top, 0);

	RECT rc;
	GetClientRect(m_hHostWindow, &rc);
	SetWindowPos(m_hMagChild, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0);

	MagSetWindowSource(m_hMagChild, m_rcCaptureScreen);
}

void MagnifierCapture::PushTask(std::function<void()> func)
{
	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockList);
		m_TaskList.push_back(func);
	}

	if (IsWindow(m_hHostWindow))
		PostMessage(m_hHostWindow, MSG_MAG_TASK, 0, 0);
}

void MagnifierCapture::RunTask()
{
	std::vector<std::function<void()>> tasks;

	{
		std::lock_guard<std::recursive_mutex> autoLock(m_lockList);
		tasks.swap(m_TaskList);
	}

	for (auto &item : tasks)
		item();
}

unsigned __stdcall MagnifierCapture::MagnifierThread(void *pParam)
{
	MagnifierCapture *self = reinterpret_cast<MagnifierCapture *>(pParam);
	self->MagnifierThreadInner();
	return 0;
}

LRESULT __stdcall MagnifierCapture::HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	MagnifierCapture *self = (MagnifierCapture *)::GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (message)
	case WM_CREATE: {
		SetTimer(hWnd, MAG_TICK_TIMER, MAG_TICK_INTERVAL, NULL);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_TIMER:
		if (self && wParam == MAG_TICK_TIMER) {
			self->CaptureVideo();
			return 0;
		}
		break;

	case MSG_MAG_TASK:
		if (self) {
			self->RunTask();
			return 0;
		}
		break;

	default:
		if (self)
			self->RunTask();

		break;
	}

		return DefWindowProc(hWnd, message, wParam, lParam);
}
