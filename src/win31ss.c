/*
 * Win31SS - a minimal Windows 3.1 (Win16) screensaver skeleton.
 *
 * Implements the standard screensaver command-line contract:
 *   (no args) or /s   - run fullscreen
 *   /c [hwnd]         - show the configuration dialog (owner window
 *                        handle is optional and currently unused)
 *   /p <hwnd>         - draw a preview embedded in the given window
 *   /a <hwnd>         - password change stub (no-op, we exit immediately)
 *
 * The visual (a bouncing circle) is a placeholder to be replaced later.
 *
 * Built with OpenWatcom's -zW flag, which generates the callback thunks
 * Windows 3.x needs for exported procedures automatically - so WndProc
 * is assigned directly with no MakeProcInstance/.def EXPORTS dance.
 * ConfigDlgProc still goes through MakeProcInstance/FreeProcInstance,
 * matching OpenWatcom's own Win16 sample code (samples/win/generic).
 */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "resource.h"

#define APP_NAME        "Win31SS"
#define WND_CLASS_NAME  "Win31SSWndClass"
#define INI_SECTION     "ScreenSaver.Win31SS"
#define INI_FILE        "CONTROL.INI"
#define KEY_SPEED       "Speed"
#define DEFAULT_SPEED   4       /* pixels moved per timer tick */
#define MIN_SPEED       1
#define MAX_SPEED       50
#define TIMER_ID        1
#define TIMER_INTERVAL  100     /* ms */
#define SHAPE_SIZE      40

typedef enum {
    MODE_FULLSCREEN,
    MODE_CONFIG,
    MODE_PREVIEW,
    MODE_PASSWORD
} SS_MODE;

static BOOL      gbFullscreen       = FALSE;
static BOOL      gbIgnoreFirstMove  = TRUE;
static int       gSpeed             = DEFAULT_SPEED;
static int       gX = 10, gY = 10, gDX = 1, gDY = 1;

/* Dirty-rect margin around the shape's bounding box, to also cover the
   1px border Ellipse() draws with the default pen. */
#define SHAPE_MARGIN    2

LONG FAR PASCAL WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL FAR PASCAL ConfigDlgProc(HWND, UINT, WPARAM, LPARAM);

static SS_MODE ParseCmdLine(LPSTR lpCmdLine, HWND FAR *phwndArg);
static void LoadSettings(void);
static void SaveSettings(void);
static void AdvanceAnimation(RECT FAR *rc);
static void DrawFrame(HDC hdc, RECT FAR *rc);
static void ShapeRect(RECT FAR *rcClient, int x, int y, RECT FAR *rcOut);

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc;
    MSG msg;
    HWND hwnd = NULL;
    HWND hwndArg = NULL;
    SS_MODE mode;

    mode = ParseCmdLine(lpCmdLine, &hwndArg);

    if (mode == MODE_PASSWORD) {
        /* No password protection support: nothing to do. */
        return 0;
    }

    if (mode == MODE_PREVIEW && hwndArg == NULL) {
        /* Malformed /p invocation - fall back to a normal run. */
        mode = MODE_FULLSCREEN;
    }

    LoadSettings();

    if (mode == MODE_CONFIG) {
        FARPROC lpfnDlgProc = MakeProcInstance((FARPROC) ConfigDlgProc, hInstance);
        DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONFIG), hwndArg, (DLGPROC) lpfnDlgProc);
        FreeProcInstance(lpfnDlgProc);
        return 0;
    }

    if (!hPrevInstance) {
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = (WNDPROC) WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = NULL;
        wc.hCursor       = (mode == MODE_FULLSCREEN) ? NULL : LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = WND_CLASS_NAME;

        if (!RegisterClass(&wc)) {
            return 0;
        }
    }

    if (mode == MODE_PREVIEW) {
        RECT rc;
        gbFullscreen = FALSE;
        GetClientRect(hwndArg, &rc);
        hwnd = CreateWindow(WND_CLASS_NAME, APP_NAME,
                             WS_CHILD | WS_VISIBLE,
                             0, 0, rc.right - rc.left, rc.bottom - rc.top,
                             hwndArg, NULL, hInstance, NULL);
    } else {
        gbFullscreen = TRUE;
        hwnd = CreateWindow(WND_CLASS_NAME, APP_NAME,
                             WS_POPUP,
                             0, 0,
                             GetSystemMetrics(SM_CXSCREEN),
                             GetSystemMetrics(SM_CYSCREEN),
                             NULL, NULL, hInstance, NULL);
    }

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, gbFullscreen ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(hwnd);

    if (gbFullscreen) {
        /* SetForegroundWindow is Win32-only; a topmost, ownerless popup
           becomes active on ShowWindow alone under Windows 3.1. */
        ShowCursor(FALSE);
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}

LONG FAR PASCAL WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    PAINTSTRUCT ps;
    RECT rc;

    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
        return 0;

    case WM_TIMER:
        {
            RECT rcOld, rcNew, rcDirty;

            GetClientRect(hwnd, &rc);
            ShapeRect(&rc, gX, gY, &rcOld);
            AdvanceAnimation(&rc);
            ShapeRect(&rc, gX, gY, &rcNew);
            UnionRect(&rcDirty, &rcOld, &rcNew);
            InvalidateRect(hwnd, &rcDirty, FALSE);
        }
        return 0;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        DrawFrame(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;

    case WM_MOUSEMOVE:
        if (gbFullscreen) {
            if (gbIgnoreFirstMove) {
                gbIgnoreFirstMove = FALSE;
            } else {
                DestroyWindow(hwnd);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (gbFullscreen) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1L; /* DrawFrame clears the background itself */

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        if (gbFullscreen) {
            ShowCursor(TRUE);
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

BOOL FAR PASCAL ConfigDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            /* No configurable controls yet - round-trip the current
               settings so the CONTROL.INI persistence path is exercised. */
            SaveSettings();
            EndDialog(hDlg, TRUE);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        default:
            break;
        }
        return FALSE;

    default:
        break;
    }

    return FALSE;
}

/* atol() takes a near "const char *" under this memory model, which would
   silently truncate lpCmdLine's far pointer - so parse the decimal digits
   by hand instead of handing the far pointer to the CRT. */
static WORD ParseWord(LPSTR p)
{
    WORD val = 0;
    while (*p >= '0' && *p <= '9') {
        val = (WORD) (val * 10 + (*p - '0'));
        p++;
    }
    return val;
}

/* Parses the screensaver command line contract: (empty)/s, /c, /p <hwnd>,
   /a <hwnd>. Accepts '/' or '-' switch prefixes and is case-insensitive. */
static SS_MODE ParseCmdLine(LPSTR lpCmdLine, HWND FAR *phwndArg)
{
    LPSTR p = lpCmdLine;
    char sw;

    *phwndArg = NULL;

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p == '\0' || (*p != '/' && *p != '-')) {
        return MODE_FULLSCREEN;
    }

    p++;
    sw = *p;
    if (sw >= 'A' && sw <= 'Z') {
        sw = (char) (sw - 'A' + 'a');
    }
    if (*p != '\0') {
        p++;
    }

    while (*p == ':' || *p == ' ' || *p == '\t') {
        p++;
    }

    if (*p != '\0') {
        *phwndArg = (HWND) ParseWord(p);
    }

    switch (sw) {
    case 'c':
        return MODE_CONFIG;
    case 'p':
        return MODE_PREVIEW;
    case 'a':
        return MODE_PASSWORD;
    case 's':
    default:
        return MODE_FULLSCREEN;
    }
}

static void LoadSettings(void)
{
    gSpeed = GetPrivateProfileInt(INI_SECTION, KEY_SPEED, DEFAULT_SPEED, INI_FILE);
    if (gSpeed < MIN_SPEED) {
        gSpeed = MIN_SPEED;
    }
    if (gSpeed > MAX_SPEED) {
        gSpeed = MAX_SPEED;
    }
    gDX = gSpeed;
    gDY = gSpeed;
}

static void SaveSettings(void)
{
    char buf[16];
    sprintf(buf, "%d", gSpeed);
    WritePrivateProfileString(INI_SECTION, KEY_SPEED, buf, INI_FILE);
}

static void ShapeRect(RECT FAR *rcClient, int x, int y, RECT FAR *rcOut)
{
    rcOut->left   = rcClient->left + x - SHAPE_MARGIN;
    rcOut->top    = rcClient->top + y - SHAPE_MARGIN;
    rcOut->right  = rcClient->left + x + SHAPE_SIZE + SHAPE_MARGIN;
    rcOut->bottom = rcClient->top + y + SHAPE_SIZE + SHAPE_MARGIN;
}

static void AdvanceAnimation(RECT FAR *rc)
{
    int width = rc->right - rc->left;
    int height = rc->bottom - rc->top;

    if (width <= SHAPE_SIZE || height <= SHAPE_SIZE) {
        return;
    }

    gX += gDX;
    gY += gDY;

    if (gX < 0) {
        gX = 0;
        gDX = -gDX;
    }
    if (gY < 0) {
        gY = 0;
        gDY = -gDY;
    }
    if (gX + SHAPE_SIZE > width) {
        gX = width - SHAPE_SIZE;
        gDX = -gDX;
    }
    if (gY + SHAPE_SIZE > height) {
        gY = height - SHAPE_SIZE;
        gDY = -gDY;
    }
}

static void DrawFrame(HDC hdc, RECT FAR *rc)
{
    HBRUSH hbrShape, hbrOld;
    int left, top;

    FillRect(hdc, rc, (HBRUSH) GetStockObject(BLACK_BRUSH));

    left = rc->left + gX;
    top = rc->top + gY;

    hbrShape = CreateSolidBrush(RGB(0, 255, 0));
    hbrOld = SelectObject(hdc, hbrShape);
    Ellipse(hdc, left, top, left + SHAPE_SIZE, top + SHAPE_SIZE);
    SelectObject(hdc, hbrOld);
    DeleteObject(hbrShape);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOut(hdc, rc->left + 4, rc->top + 4, APP_NAME, (int) strlen(APP_NAME));
}
