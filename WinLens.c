/*
 * WindowSpy.c — Mouse & Window Identifier
 * Build:  gcc -o WindowSpy.exe WindowSpy.c -mwindows -s -O2
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ── Layout ──────────────────────────────────────────────── */
#define WW   320
#define WH   420
#define TH   30
#define PAD  14
#define RH   21

/* ── 动态加载的函数类型 ──────────────────────────────────── */
typedef BOOL  (WINAPI *fn_QFPIN)(HANDLE, DWORD, LPSTR, PDWORD);
typedef BOOL  (WINAPI *fn_DPI)(void);

static fn_QFPIN pQFPIN;          /* QueryFullProcessImageNameA */
static fn_DPI   pDPI;            /* SetProcessDPIAware         */

/* ── State ───────────────────────────────────────────────── */
static HFONT  fNorm, fBold;
static HBRUSH brBg, brBar, brLine;
static HWND   selfH, tgtH;
static int    frozen;

static char vScr[32], vRel[32], vCur[32], vHdl[20];
static char vTtl[80], vCls[40], vPrc[64], vPid[16], vVis[8];
static char vPos[32], vSiz[32], vRc[64];

/* ── Cursor Name ─────────────────────────────────────────── */
static const char *curName(void)
{
    static const struct { WORD id; const char *nm; } M[] = {
        {32512, "Arrow"},        {32513, "IBeam"},
        {32514, "Wait"},         {32515, "Crosshair"},
        {32516, "Up Arrow"},     {32642, "Resize NW-SE"},
        {32643, "Resize NE-SW"}, {32644, "Resize H"},
        {32645, "Resize V"},     {32646, "Size All"},
        {32648, "Forbidden"},    {32649, "Hand"},
        {32650, "App Starting"}, {32651, "Help"},
    };
    CURSORINFO ci;
    int i, n;
    ci.cbSize = sizeof(ci);
    ci.flags  = 0;
    ci.hCursor = NULL;
    ci.ptScreenPos.x = ci.ptScreenPos.y = 0;
    n = (int)(sizeof(M) / sizeof(M[0]));
    GetCursorInfo(&ci);
    for (i = 0; i < n; i++)
        if (ci.hCursor == LoadCursorA(NULL, MAKEINTRESOURCEA(M[i].id)))
            return M[i].nm;
    return "Custom";
}

/* ── Refresh Data ────────────────────────────────────────── */
static void refresh(void)
{
    POINT pt;
    HWND  hw;
    RECT  rc;
    DWORD pid = 0;
    HANDLE hp;

    GetCursorPos(&pt);
    sprintf(vScr, "(%d, %d)", pt.x, pt.y);
    strcpy(vCur, curName());

    hw = WindowFromPoint(pt);
    if (hw) hw = GetAncestor(hw, GA_ROOT);
    if (hw && hw != selfH && IsWindow(hw)) tgtH = hw;

    if (!tgtH || !IsWindow(tgtH)) {
        tgtH = NULL;
        strcpy(vHdl, "---"); strcpy(vTtl, "");  strcpy(vCls, "---");
        strcpy(vPrc, "---"); strcpy(vPid, "---"); strcpy(vVis, "---");
        strcpy(vPos, "---"); strcpy(vSiz, "---"); strcpy(vRc,  "---");
        strcpy(vRel, "---");
        return;
    }
    hw = tgtH;

    sprintf(vHdl, "0x%08X", (unsigned)(UINT_PTR)hw);
    GetWindowTextA(hw, vTtl, 80);
    if (!vTtl[0]) strcpy(vTtl, "(untitled)");
    GetClassNameA(hw, vCls, 40);

    GetWindowThreadProcessId(hw, &pid);
    sprintf(vPid, "%lu", (unsigned long)pid);

    /* 进程名 — 动态加载 QueryFullProcessImageNameA */
    vPrc[0] = 0;
    hp = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid);
    if (hp) {
        if (pQFPIN) {
            DWORD n = MAX_PATH;
            char buf[MAX_PATH] = "";
            if (pQFPIN(hp, 0, buf, &n)) {
                char *s = strrchr(buf, '\\');
                strcpy(vPrc, s ? s + 1 : buf);
            }
        }
        if (!vPrc[0]) {
            /* 回退：用 GetModuleFileNameEx（kernel32 里一定有） */
            typedef DWORD (WINAPI *fn_GMFNE)(HANDLE, HMODULE, LPSTR, DWORD);
            fn_GMFNE pGMFNE = (fn_GMFNE)
                GetProcAddress(GetModuleHandleA("kernel32.dll"),
                               "GetModuleFileNameExA");
            if (!pGMFNE)
                pGMFNE = (fn_GMFNE)
                    GetProcAddress(GetModuleHandleA("psapi.dll"),
                                   "GetModuleFileNameExA");
            if (pGMFNE) {
                char buf[MAX_PATH] = "";
                if (pGMFNE(hp, NULL, buf, MAX_PATH)) {
                    char *s = strrchr(buf, '\\');
                    strcpy(vPrc, s ? s + 1 : buf);
                }
            }
        }
        CloseHandle(hp);
    }
    if (!vPrc[0]) strcpy(vPrc, "---");

    GetWindowRect(hw, &rc);
    sprintf(vPos, "(%d, %d)", rc.left, rc.top);
    sprintf(vSiz, "%d x %d",  rc.right - rc.left, rc.bottom - rc.top);
    sprintf(vRc,  "%d, %d, %d, %d", rc.left, rc.top, rc.right, rc.bottom);
    sprintf(vRel, "(%d, %d)", pt.x - rc.left, pt.y - rc.top);
    strcpy(vVis, IsWindowVisible(hw) ? "Yes" : "No");
}

/* ── Drawing ─────────────────────────────────────────────── */
static void out(HDC dc, int x, int y, COLORREF c, HFONT f, const char *s)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    SelectObject(dc, f);
    TextOutA(dc, x, y, s, (int)strlen(s));
}

static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC    dc  = BeginPaint(hwnd, &ps);
    HDC    mc  = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, WW, WH);
    HBITMAP ob = (HBITMAP)SelectObject(mc, bm);
    RECT   rc  = {0, 0, WW, WH};
    int    y;

    FillRect(mc, &rc, brBg);

    /* Title bar */
    rc.right = WW; rc.bottom = TH;
    FillRect(mc, &rc, brBar);
    out(mc, PAD,   8, RGB(91,154,255),  fBold, "WINDOW SPY");
    out(mc, WW-22, 8, RGB(100,100,120), fNorm, "X");
    out(mc, WW-78, 8,
        frozen ? RGB(255,140,91) : RGB(100,100,120),
        fNorm, frozen ? "FROZEN" : "FREEZE");
    rc.left = 0; rc.top = TH; rc.right = WW; rc.bottom = TH + 1;
    FillRect(mc, &rc, brLine);

    /* Content */
    y = TH + 10;

    out(mc, PAD, y, RGB(91,154,255), fBold, "CURSOR");         y += 22;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Screen");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vScr);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Relative");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vRel);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Cursor");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vCur);         y += RH + 8;

    out(mc, PAD, y, RGB(91,154,255), fBold, "TARGET WINDOW");  y += 22;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Handle");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vHdl);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Title");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vTtl);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Class");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vCls);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Process");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vPrc);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "PID");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vPid);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Visible");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vVis);         y += RH + 8;

    out(mc, PAD, y, RGB(91,154,255), fBold, "GEOMETRY");       y += 22;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Position");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vPos);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "Size");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vSiz);         y += RH;
    out(mc, PAD, y, RGB(72,72,96),   fNorm, "LTRB");
    out(mc, PAD+85, y, RGB(208,212,222), fNorm, vRc);

    out(mc, PAD, WH - 22, RGB(72,72,96), fNorm,
        "Drag to move  |  Esc close  |  F freeze");

    BitBlt(dc, 0, 0, WW, WH, mc, 0, 0, SRCCOPY);
    SelectObject(mc, ob);
    DeleteObject(bm);
    DeleteDC(mc);
    EndPaint(hwnd, &ps);
}

/* ── WndProc ─────────────────────────────────────────────── */
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        SetTimer(h, 1, 33, NULL);
        return 0;
    case WM_TIMER:
        if (!frozen) { refresh(); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_LBUTTONDOWN:
        if (HIWORD(l) < TH) {
            int x = LOWORD(l);
            if (x > WW - 32)  { DestroyWindow(h); return 0; }
            if (x > WW - 90)  { frozen ^= 1;      return 0; }
            ReleaseCapture();
            SendMessage(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    case WM_KEYDOWN:
        if      (w == VK_ESCAPE) DestroyWindow(h);
        else if (w == 'F')       frozen ^= 1;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return  0;
    }
    return DefWindowProc(h, m, w, l);
}

/* ── Entry ───────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int cs)
{
    WNDCLASSEXA wc;
    MSG msg;
    HMODULE hm;
    int sx;

    (void)hp; (void)cl;

    /* 全部动态加载，零 import lib 依赖 */
    hm = GetModuleHandleA("kernel32.dll");
    if (hm) {
        pQFPIN = (fn_QFPIN)GetProcAddress(hm, "QueryFullProcessImageNameA");
        pDPI   = (fn_DPI)  GetProcAddress(hm, "SetProcessDPIAware");
    }
    /* DPI 也尝试 shcore */
    if (!pDPI) {
        hm = GetModuleHandleA("shcore.dll");
        if (!hm) hm = LoadLibraryA("shcore.dll");
        if (hm) {
            typedef HRESULT (WINAPI *fn_SPDA)(int);
            fn_SPDA fn = (fn_SPDA)GetProcAddress(hm, "SetProcessDpiAwareness");
            if (fn) fn(2);
        }
    } else {
        pDPI();
    }

    fNorm  = CreateFontA(-13,0,0,0, 400,0,0,0,0,0,0,0,0, "Consolas");
    fBold  = CreateFontA(-13,0,0,0, 700,0,0,0,0,0,0,0,0, "Consolas");
    brBg   = CreateSolidBrush(RGB(10,10,17));
    brBar  = CreateSolidBrush(RGB(17,17,25));
    brLine = CreateSolidBrush(RGB(30,30,48));

    strcpy(vScr,"---"); strcpy(vRel,"---"); strcpy(vCur,"---");
    strcpy(vHdl,"---"); strcpy(vTtl,"");   strcpy(vCls,"---");
    strcpy(vPrc,"---"); strcpy(vPid,"---"); strcpy(vVis,"---");
    strcpy(vPos,"---"); strcpy(vSiz,"---"); strcpy(vRc, "---");

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wp;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WinSpy";
    RegisterClassExA(&wc);

    sx = GetSystemMetrics(SM_CXSCREEN) - WW - 20;
    selfH = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "WinSpy", "WindowSpy", WS_POPUP,
        sx, 40, WW, WH, NULL, NULL, hi, NULL);

    ShowWindow(selfH, cs);
    UpdateWindow(selfH);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(fNorm); DeleteObject(fBold);
    DeleteObject(brBg);  DeleteObject(brBar); DeleteObject(brLine);
    return (int)msg.wParam;
}
