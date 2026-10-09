// Identity Autofill for Windows.
//
// Shows a small always-on-top window with a "Fill" button. Click the first box
// of a form (e.g. email), then press Fill (or F9): a fresh identity is loaded
// from the Canada Identity Panel and typed into the form in the order of the
// lines in identity.txt, pressing Tab between them. If the panel can't be
// reached, the values written in identity.txt are typed instead.
//
// Build: x86_64-w64-mingw32-gcc -O2 -municode -mwindows autofill.c -o IdentityAutofill.exe -luser32 -lgdi32 -lshell32 -lwinhttp

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <stdio.h>
#include <wchar.h>
#include "panel.h"

#define ID_FILL 1
#define ID_EDIT 2
#define ID_HOTKEY 100
#define WM_FILL_DONE (WM_APP + 1)

#define MAX_LINES 64
#define MAX_VALUE 512
#define DEFAULT_PANEL L"http://localhost:8085"

static const char DEFAULT_IDENTITY[] =
    "# Identity Autofill\r\n"
    "# 1. Click the FIRST box of the form (the email box).\r\n"
    "# 2. Press Fill (or F9). Press Esc to stop typing.\r\n"
    "#\r\n"
    "# Each time you press Fill, a new identity is loaded from the panel below\r\n"
    "# and used in place of the values here. Set  panel = off  to always use\r\n"
    "# these values. They're also used if the panel can't be reached.\r\n"
    "panel = http://localhost:8085\r\n"
    "#\r\n"
    "# The lines below are typed in order, with Tab pressed between lines.\r\n"
    "# Only the text after '=' is typed. Lines starting with # are ignored.\r\n"
    "# Special keys you can use in a value:\r\n"
    "#   {tab} {space} {enter} {skip} (leave a box empty) {wait} (pause 0.5s)\r\n"
    "#\r\n"
    "email = pandahax7221@gmail.com\r\n"
    "first name = Bob\r\n"
    "last name = Martin\r\n"
    "street address = 123 Barney Avenue\r\n"
    "postal code = M2K 9E4\r\n"
    "province = Ontario\r\n"
    "municipality = Toronto\r\n"
    "mobile phone = 4379218267\r\n"
    "birth month = MAR\r\n"
    "birth day = 04\r\n"
    "birth year = 1980\r\n"
    "# Tab lands on the Male button; {space} presses it.\r\n"
    "# For Female use:  gender = {tab}{space}\r\n"
    "gender = {space}\r\n";

static HWND g_wnd, g_fillBtn, g_status;
static volatile LONG g_busy;
static WCHAR g_identityPath[MAX_PATH];
static WCHAR g_values[MAX_LINES][MAX_VALUE];
static int g_keys[MAX_LINES];
static int g_count;
static WCHAR g_panelUrl[MAX_VALUE];
static WCHAR g_statusText[256];

static void SetStatus(const WCHAR *text) { SetWindowTextW(g_status, text); }

static void InitIdentityPath(void) {
    GetModuleFileNameW(NULL, g_identityPath, MAX_PATH);
    WCHAR *slash = wcsrchr(g_identityPath, L'\\');
    if (slash) slash[1] = 0; else g_identityPath[0] = 0;
    wcscat(g_identityPath, L"identity.txt");

    if (GetFileAttributesW(g_identityPath) == INVALID_FILE_ATTRIBUTES) {
        HANDLE f = CreateFileW(g_identityPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(f, DEFAULT_IDENTITY, sizeof(DEFAULT_IDENTITY) - 1, &written, NULL);
            CloseHandle(f);
        }
    }
}

static WCHAR *Trim(WCHAR *s) {
    while (*s == L' ' || *s == L'\t') s++;
    WCHAR *e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r')) *--e = 0;
    return s;
}

// Reads identity.txt (UTF-8) into g_values. Returns FALSE if the file can't be read.
static BOOL LoadIdentity(void) {
    g_count = 0;
    lstrcpynW(g_panelUrl, DEFAULT_PANEL, MAX_VALUE);
    HANDLE f = CreateFileW(g_identityPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD size = GetFileSize(f, NULL), read = 0;
    char *buf = HeapAlloc(GetProcessHeap(), 0, size + 1);
    ReadFile(f, buf, size, &read, NULL);
    CloseHandle(f);
    buf[read] = 0;

    char *text = buf;
    if (read >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) text += 3;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0, wlen * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, wlen);
    HeapFree(GetProcessHeap(), 0, buf);

    for (WCHAR *line = wide, *next; line && g_count < MAX_LINES; line = next) {
        next = wcschr(line, L'\n');
        if (next) *next++ = 0;
        line = Trim(line);
        if (!*line || *line == L'#') continue;
        WCHAR *eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = 0;
        WCHAR *label = Trim(line), *value = Trim(eq + 1);
        if (!_wcsicmp(label, L"panel")) {
            lstrcpynW(g_panelUrl, value, MAX_VALUE);
            continue;
        }
        g_keys[g_count] = Panel_LabelKey(label);
        lstrcpynW(g_values[g_count++], value, MAX_VALUE);
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return TRUE;
}

// GETs a URL; returns the body as a heap-allocated wide string, or NULL.
static WCHAR *HttpGet(const WCHAR *url) {
    URL_COMPONENTS uc = {0};
    WCHAR host[256], path[1024];
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return NULL;
    if (!path[0]) wcscpy(path, L"/");

    WCHAR *result = NULL;
    char *body = NULL;
    DWORD len = 0;
    HINTERNET s = WinHttpOpen(L"IdentityAutofill/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (s) WinHttpSetTimeouts(s, 3000, 3000, 3000, 5000);
    if (r && WinHttpSendRequest(r, L"Accept: application/json, text/html;q=0.9, */*;q=0.8\r\n", (DWORD)-1, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
        if (status == 200) {
            DWORD avail;
            while (WinHttpQueryDataAvailable(r, &avail) && avail && len < 4 * 1024 * 1024) {
                char *grown = body ? HeapReAlloc(GetProcessHeap(), 0, body, len + avail + 1) : HeapAlloc(GetProcessHeap(), 0, avail + 1);
                if (!grown) break;
                body = grown;
                DWORD got = 0;
                if (!WinHttpReadData(r, body + len, avail, &got) || !got) break;
                len += got;
            }
        }
    }
    if (body) {
        body[len] = 0;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, body, -1, NULL, 0);
        result = HeapAlloc(GetProcessHeap(), 0, wlen * sizeof(WCHAR));
        MultiByteToWideChar(CP_UTF8, 0, body, -1, result, wlen);
        HeapFree(GetProcessHeap(), 0, body);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return result;
}

// Loads a fresh identity from the panel and swaps it into g_values.
// Returns TRUE if the panel was used.
static BOOL LoadFromPanel(void) {
    if (!g_panelUrl[0] || !_wcsicmp(g_panelUrl, L"off")) return FALSE;
    static const WCHAR *paths[] = {L"", L"/api/identity", L"/identity", L"/api/generate", L"/generate", L"/api", L"/random"};
    WCHAR base[MAX_VALUE], url[MAX_VALUE + 32];
    lstrcpynW(base, g_panelUrl, MAX_VALUE);
    size_t n = wcslen(base);
    while (n && base[n - 1] == L'/') base[--n] = 0;

    PanelData d;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        swprintf(url, MAX_VALUE + 32, L"%ls%ls", base, paths[i]);
        WCHAR *body = HttpGet(url);
        if (!body) {
            if (i == 0) return FALSE;  // panel isn't running at all
            continue;
        }
        int found = Panel_Parse(&d, body);
        HeapFree(GetProcessHeap(), 0, body);
        if (found < 3) continue;

        for (int k = 0; k < g_count; k++) {
            WCHAR v[MAX_VALUE];
            Panel_Format(&d, g_keys[k], v, MAX_VALUE);
            if (v[0]) lstrcpynW(g_values[k], v, MAX_VALUE);
        }
        swprintf(g_statusText, 256, L"Loaded %ls %ls from panel. Typing... (Esc stops)", d.v[K_FIRST], d.v[K_LAST]);
        return TRUE;
    }
    return FALSE;
}

static BOOL Aborted(void) { return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0; }

static void SendVk(WORD vk) {
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(40);
}

static void SendChar(WCHAR c) {
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wScan = in[1].ki.wScan = c;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(15);
}

// Types one value, expanding {tab}/{space}/{enter}/{skip}/{wait}.
static BOOL TypeValue(const WCHAR *v) {
    while (*v) {
        if (Aborted()) return FALSE;
        if (*v == L'{') {
            const WCHAR *close = wcschr(v, L'}');
            if (close) {
                size_t n = close - v + 1;
                if (!_wcsnicmp(v, L"{tab}", n)) SendVk(VK_TAB);
                else if (!_wcsnicmp(v, L"{space}", n)) SendVk(VK_SPACE);
                else if (!_wcsnicmp(v, L"{enter}", n)) SendVk(VK_RETURN);
                else if (!_wcsnicmp(v, L"{wait}", n)) Sleep(500);
                else if (_wcsnicmp(v, L"{skip}", n)) { for (size_t i = 0; i < n; i++) SendChar(v[i]); }
                v = close + 1;
                continue;
            }
        }
        SendChar(*v++);
    }
    return TRUE;
}

static DWORD WINAPI FillThread(LPVOID arg) {
    (void)arg;
    if (!LoadFromPanel()) wcscpy(g_statusText, L"Panel not found; typing identity.txt... (Esc stops)");
    SetStatus(g_statusText);
    // Let the mouse click / F9 key finish so it doesn't land in the form.
    while ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_F9) & 0x8000)) Sleep(20);
    Sleep(150);

    BOOL ok = TRUE;
    for (int i = 0; i < g_count && ok; i++) {
        ok = TypeValue(g_values[i]);
        if (ok && i < g_count - 1) { SendVk(VK_TAB); Sleep(60); }
    }
    PostMessageW(g_wnd, WM_FILL_DONE, ok, 0);
    return 0;
}

static void StartFill(void) {
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    if (!LoadIdentity()) {
        SetStatus(L"Can't read identity.txt");
        g_busy = 0;
        return;
    }
    if (g_count == 0) {
        SetStatus(L"identity.txt has no 'name = value' lines");
        g_busy = 0;
        return;
    }
    SetStatus(L"Loading identity from panel...");
    EnableWindow(g_fillBtn, FALSE);
    CloseHandle(CreateThread(NULL, 0, FillThread, NULL, 0, NULL));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_FILL) StartFill();
        else if (LOWORD(wp) == ID_EDIT) ShellExecuteW(hwnd, L"open", L"notepad.exe", g_identityPath, NULL, SW_SHOWNORMAL);
        return 0;
    case WM_HOTKEY:
        if (wp == ID_HOTKEY) StartFill();
        return 0;
    case WM_FILL_DONE:
        SetStatus(wp ? L"Done. Click a form's first box, then Fill / F9" : L"Stopped.");
        EnableWindow(g_fillBtn, TRUE);
        g_busy = 0;
        return 0;
    case WM_MOUSEACTIVATE:
        // Clicking our window must not steal focus from the browser.
        return MA_NOACTIVATE;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, ID_HOTKEY);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd; (void)show;
    SetProcessDPIAware();
    InitIdentityPath();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"IdentityAutofill";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    const int W = 300, H = 120;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Identity Autofill",
                            WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                            work.right - W - 20, work.bottom - H - 20, W, H, NULL, NULL, inst, NULL);

    HFONT font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT small = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    RECT rc;
    GetClientRect(g_wnd, &rc);
    int cw = rc.right;
    g_fillBtn = CreateWindowW(L"BUTTON", L"Fill  (F9)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              10, 10, cw - 100, 40, g_wnd, (HMENU)ID_FILL, inst, NULL);
    HWND edit = CreateWindowW(L"BUTTON", L"Edit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              cw - 82, 10, 72, 40, g_wnd, (HMENU)ID_EDIT, inst, NULL);
    g_status = CreateWindowW(L"STATIC", L"Click a form's first box, then Fill / F9", WS_CHILD | WS_VISIBLE,
                             10, 58, cw - 20, 20, g_wnd, NULL, inst, NULL);
    SendMessageW(g_fillBtn, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_status, WM_SETFONT, (WPARAM)small, TRUE);

    if (!RegisterHotKey(g_wnd, ID_HOTKEY, MOD_NOREPEAT, VK_F9))
        SetStatus(L"F9 is taken by another app; use the button");

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
