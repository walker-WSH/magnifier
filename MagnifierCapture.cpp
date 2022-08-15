#include "pch.h"
#include "MagnifierCapture.h"

#pragma comment(lib, "Magnification.lib")

#define MAG_WINDOW_CLASS TEXT("MagnifierWindow")
#define MAG_TICK_TIMER 1000
#define MAG_TICK_INTERVAL 40 // in ms

MagnifierCapture::MagnifierCapture()
{
	RegisterMagClass();
}

MagnifierCapture::~MagnifierCapture()
{
	UnregisterClass(MAG_WINDOW_CLASS, GetModuleHandle(0));
}

void MagnifierCapture::Start(RECT rcScreen, std::vector<HWND> filter)
{
	m_vIgnore = filter;
	m_rcCaptureScreen = rcScreen;

	m_hExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hMagThread = (HANDLE)_beginthreadex(0, 0, MagnifierThread, this, 0, 0);
}

void MagnifierCapture::Stop()
{
	// TODO: Wait init done

	if (IsWindow(m_hHostWindow))
		PostMessage(m_hHostWindow, WM_CLOSE, 0, 0);

	SetEvent(m_hExitEvent);
	WaitForSingleObject(m_hMagThread, INFINITE);

	CloseHandle(m_hMagThread);
	CloseHandle(m_hExitEvent);

	m_hMagThread = 0;
	m_hExitEvent = 0;
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

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	MagUninitialize();
}

BOOL MagnifierCapture::SetupMagnifier(HINSTANCE hInst)
{
	DWORD dwStyle = WS_SIZEBOX | WS_CLIPCHILDREN | WS_MAXIMIZEBOX;
	m_hHostWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED, MAG_WINDOW_CLASS, TEXT("Screen Magnifier Sample"), dwStyle, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
	if (!m_hHostWindow)
		return FALSE;

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
		return FALSE;
	}

	if (!m_vIgnore.empty())
		MagSetWindowFilterList(m_hMagChild, MW_FILTERMODE_EXCLUDE, (int)m_vIgnore.size(), m_vIgnore.data());

	if (DEBUG_MAG_WINDOW) {
		MAGCOLOREFFECT magEffectInvert = {{// MagEffectInvert
						   {-1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
						   {0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
						   {0.0f, 0.0f, -1.0f, 0.0f, 0.0f},
						   {0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
						   {1.0f, 1.0f, 1.0f, 0.0f, 1.0f}}};

		MagSetColorEffect(m_hMagChild, &magEffectInvert);
	}

	return TRUE;
}

void MagnifierCapture::UpdateMagWindow()
{
	int cx = m_rcCaptureScreen.right - m_rcCaptureScreen.left;
	int cy = m_rcCaptureScreen.bottom - m_rcCaptureScreen.top;
	SetWindowPos(m_hHostWindow, NULL, m_rcCaptureScreen.left, m_rcCaptureScreen.top, cx, cy, 0);

	RECT rc;
	GetClientRect(m_hHostWindow, &rc);
	SetWindowPos(m_hMagChild, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0);

	MagSetWindowSource(m_hMagChild, m_rcCaptureScreen);
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

	case WM_TIMER:
		if (self && wParam == MAG_TICK_TIMER) {
			self->UpdateMagWindow();
			return 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}

		return DefWindowProc(hWnd, message, wParam, lParam);
}
