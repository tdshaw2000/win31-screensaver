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
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "resource.h"

#define APP_NAME        "Win31SS"
#define WND_CLASS_NAME  "Win31SSWndClass"
#define INI_SECTION     "ScreenSaver.Win31SS"
#define INI_FILE        "CONTROL.INI"
#define KEY_SPEED       "Speed"
#define DEFAULT_SPEED   2       /* pixels moved per timer tick */
#define MIN_SPEED       1
#define MAX_SPEED       25
#define KEY_COLOR       "Color"
#define DEFAULT_COLOR   0       /* index into gColors[] */
#define KEY_SIZE        "Size"
#define DEFAULT_SIZE    40      /* ball diameter, in pixels */
#define MIN_SIZE        10
#define MAX_SIZE        100
#define KEY_SIDES       "Sides"
#define DEFAULT_SIDES   1       /* 1 = circle, matches the pre-shape behavior */
#define MIN_SIDES       1
#define MAX_SIDES       15      /* max sides of an actual drawn polygon */
#define IDX_SIDES_CYCLE     (MAX_SIDES + 1) /* steps through every shape on each bounce */
#define IDX_SIDES_RANDOM    (MAX_SIDES + 2) /* jumps to a random shape on each bounce */
#define MAX_SIDES_SETTING   IDX_SIDES_RANDOM /* upper bound of the Sides *setting* */
#define KEY_TRAIL       "Trail"
#define DEFAULT_TRAIL   5       /* number of fading ghosts behind the shape */
#define MIN_TRAIL       0       /* 0 = no trail */
#define MAX_TRAIL       10
#define TIMER_ID        1
#define TIMER_INTERVAL  50      /* ms - close to Win16's ~55ms clock-tick floor */
#define PI              3.14159265358979323846

typedef struct {
    char        *name;
    COLORREF    color;
} COLOR_ENTRY;

/* Indexed directly by side count; index 0 and 2 are unused (0 doesn't
   occur, and 2 - a degenerate polygon - is skipped by the UI). Indices
   past MAX_SIDES are the Cycle/Random modes, not real side counts. */
static char *gShapeNames[MAX_SIDES_SETTING + 1] = {
    NULL, "Circle", NULL, "Triangle", "Square", "Pentagon", "Hexagon",
    "Heptagon", "Octagon", "Nonagon", "Decagon", "Hendecagon", "Dodecagon",
    "Tridecagon", "Tetradecagon", "Pentadecagon", "Cycle", "Random"
};

static COLOR_ENTRY gColors[] = {
    { "Green",   RGB(0, 255, 0) },
    { "Red",     RGB(255, 0, 0) },
    { "Blue",    RGB(0, 0, 255) },
    { "Yellow",  RGB(255, 255, 0) },
    { "White",   RGB(255, 255, 255) },
    { "Magenta", RGB(255, 0, 255) },
};
#define NUM_COLORS (sizeof(gColors) / sizeof(gColors[0]))
#define RAINBOW_NAME    "Rainbow"
#define IDX_RAINBOW     ((int) NUM_COLORS) /* combobox item after the last real color */
#define NUM_ITEMS       ((int) NUM_COLORS + 1)

typedef enum {
    MODE_FULLSCREEN,
    MODE_CONFIG,
    MODE_PREVIEW,
    MODE_PASSWORD
} SS_MODE;

static BOOL      gbFullscreen       = FALSE;
static BOOL      gbIgnoreFirstMove  = TRUE;
static int       gSpeed             = DEFAULT_SPEED;
static int       gColorIndex        = DEFAULT_COLOR;
static int       gRainbowIndex      = 0;   /* current color while gColorIndex == IDX_RAINBOW */
static int       gShapeSize         = DEFAULT_SIZE;
static int       gSides             = DEFAULT_SIDES;
static int       gCurrentSides      = DEFAULT_SIDES; /* side count actually drawn this frame */
static int       gX = 10, gY = 10, gDX = 1, gDY = 1;

typedef struct {
    int      x, y;
    int      sides;
    COLORREF color;
} TRAIL_ENTRY;

static TRAIL_ENTRY gTrail[MAX_TRAIL];
static int         gTrailLength = DEFAULT_TRAIL;
static int         gTrailHead   = 0;  /* next slot to overwrite */
static int         gTrailCount  = 0;  /* valid entries, ramps 0..gTrailLength */

/* Offscreen buffer for double-buffered painting: WM_PAINT draws a full
   frame here, then BitBlt's just the dirty rect to the screen in one
   atomic call, so the screen never shows a mid-erase frame. */
static HDC       gHdcMem    = NULL;
static HBITMAP   gHbmMem    = NULL;
static HBITMAP   gHbmMemOld = NULL;

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
static void DrawShapeAt(HDC hdc, int left, int top, int sides, COLORREF color);
static COLORREF CurrentColor(void);
static COLORREF DimColor(COLORREF color, int age, int maxAge);
static void PushTrail(int x, int y, int sides, COLORREF color);
static void ShapeRect(RECT FAR *rcClient, int x, int y, RECT FAR *rcOut);
static void SetSidesLabel(HWND hDlg, int sides);
static int  NextValidSides(int sides);
static int  RandomValidSides(void);

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

    srand((unsigned) GetTickCount());
    LoadSettings();

    if (mode == MODE_CONFIG) {
        FARPROC lpfnDlgProc = MakeProcInstance((FARPROC) ConfigDlgProc, hInstance);
        DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONFIG), hwndArg, (DLGPROC) lpfnDlgProc);
        FreeProcInstance(lpfnDlgProc);
        return 0;
    }

    if (mode == MODE_FULLSCREEN && hPrevInstance != NULL) {
        /* The screensaver launcher can occasionally re-fire its idle timer
           while an instance is still animating; each instance gets its own
           fresh globals in Win16, so a second one would appear as the
           shape "restarting" at (10, 10) in a new window layered over the
           first. Exit instead of duplicating it. */
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
        {
            HDC hdcScreen = GetDC(hwnd);
            GetClientRect(hwnd, &rc);
            gHdcMem = CreateCompatibleDC(hdcScreen);
            if (gHdcMem != NULL) {
                gHbmMem = CreateCompatibleBitmap(hdcScreen, rc.right - rc.left, rc.bottom - rc.top);
                if (gHbmMem != NULL) {
                    gHbmMemOld = SelectObject(gHdcMem, gHbmMem);
                } else {
                    DeleteDC(gHdcMem);
                    gHdcMem = NULL;
                }
            }
            ReleaseDC(hwnd, hdcScreen);
        }
        SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
        return 0;

    case WM_TIMER:
        {
            RECT rcOld, rcNew, rcDirty, rcTrail;
            int i;

            GetClientRect(hwnd, &rc);
            ShapeRect(&rc, gX, gY, &rcOld);

            rcDirty = rcOld;
            for (i = 0; i < gTrailCount; i++) {
                ShapeRect(&rc, gTrail[i].x, gTrail[i].y, &rcTrail);
                UnionRect(&rcDirty, &rcDirty, &rcTrail);
            }

            PushTrail(gX, gY, gCurrentSides, CurrentColor());

            AdvanceAnimation(&rc);
            ShapeRect(&rc, gX, gY, &rcNew);
            UnionRect(&rcDirty, &rcDirty, &rcNew);
            InvalidateRect(hwnd, &rcDirty, FALSE);
        }
        return 0;

    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        if (gHdcMem != NULL) {
            DrawFrame(gHdcMem, &rc);
            BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
                   ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top,
                   gHdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        } else {
            DrawFrame(hdc, &rc);
        }
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
        if (gHdcMem != NULL) {
            SelectObject(gHdcMem, gHbmMemOld);
            DeleteObject(gHbmMem);
            DeleteDC(gHdcMem);
            gHdcMem = NULL;
        }
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
        {
            int i;
            for (i = 0; i < (int) NUM_COLORS; i++) {
                SendDlgItemMessage(hDlg, IDC_COLOR, CB_ADDSTRING, 0,
                                    (LPARAM) (LPSTR) gColors[i].name);
            }
            SendDlgItemMessage(hDlg, IDC_COLOR, CB_ADDSTRING, 0, (LPARAM) (LPSTR) RAINBOW_NAME);
            SendDlgItemMessage(hDlg, IDC_COLOR, CB_SETCURSEL, gColorIndex, 0);

            SetScrollRange(GetDlgItem(hDlg, IDC_BALLSIZE), SB_CTL, MIN_SIZE, MAX_SIZE, FALSE);
            SetScrollPos(GetDlgItem(hDlg, IDC_BALLSIZE), SB_CTL, gShapeSize, TRUE);
            SetDlgItemInt(hDlg, IDC_BALLSIZE_VALUE, (UINT) gShapeSize, FALSE);

            SetScrollRange(GetDlgItem(hDlg, IDC_SIDES), SB_CTL, MIN_SIDES, MAX_SIDES_SETTING, FALSE);
            SetScrollPos(GetDlgItem(hDlg, IDC_SIDES), SB_CTL, gSides, TRUE);
            SetSidesLabel(hDlg, gSides);

            SetScrollRange(GetDlgItem(hDlg, IDC_SPEED), SB_CTL, MIN_SPEED, MAX_SPEED, FALSE);
            SetScrollPos(GetDlgItem(hDlg, IDC_SPEED), SB_CTL, gSpeed, TRUE);
            SetDlgItemInt(hDlg, IDC_SPEED_VALUE, (UINT) gSpeed, FALSE);

            SetScrollRange(GetDlgItem(hDlg, IDC_TRAIL), SB_CTL, MIN_TRAIL, MAX_TRAIL, FALSE);
            SetScrollPos(GetDlgItem(hDlg, IDC_TRAIL), SB_CTL, gTrailLength, TRUE);
            SetDlgItemInt(hDlg, IDC_TRAIL_VALUE, (UINT) gTrailLength, FALSE);
        }
        return TRUE;

    /* Win16's WM_HSCROLL carries the notification code in wParam, and the
       thumb position (THUMBTRACK/THUMBPOSITION only) plus the control's
       handle in LOWORD/HIWORD(lParam) - unlike Win32, which packs code and
       position into wParam and passes just the handle in lParam. */
    case WM_HSCROLL:
        {
            HWND hwndScroll = (HWND) HIWORD(lParam);
            BOOL bIncrement;
            int pos;

            if (hwndScroll == GetDlgItem(hDlg, IDC_BALLSIZE)) {
                pos = GetScrollPos(hwndScroll, SB_CTL);
                switch (wParam) {
                case SB_LINELEFT:
                    pos -= 1;
                    break;
                case SB_LINERIGHT:
                    pos += 1;
                    break;
                case SB_PAGELEFT:
                    pos -= 10;
                    break;
                case SB_PAGERIGHT:
                    pos += 10;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    pos = (int) LOWORD(lParam);
                    break;
                case SB_LEFT:
                    pos = MIN_SIZE;
                    break;
                case SB_RIGHT:
                    pos = MAX_SIZE;
                    break;
                default:
                    break;
                }
                if (pos < MIN_SIZE) {
                    pos = MIN_SIZE;
                }
                if (pos > MAX_SIZE) {
                    pos = MAX_SIZE;
                }
                SetScrollPos(hwndScroll, SB_CTL, pos, TRUE);
                SetDlgItemInt(hDlg, IDC_BALLSIZE_VALUE, (UINT) pos, FALSE);
                return 0;
            }

            if (hwndScroll == GetDlgItem(hDlg, IDC_SIDES)) {
                pos = GetScrollPos(hwndScroll, SB_CTL);
                bIncrement = FALSE;
                switch (wParam) {
                case SB_LINELEFT:
                    pos -= 1;
                    break;
                case SB_LINERIGHT:
                    pos += 1;
                    bIncrement = TRUE;
                    break;
                case SB_PAGELEFT:
                    pos -= 10;
                    break;
                case SB_PAGERIGHT:
                    pos += 10;
                    bIncrement = TRUE;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    bIncrement = ((int) LOWORD(lParam) >= pos);
                    pos = (int) LOWORD(lParam);
                    break;
                case SB_LEFT:
                    pos = MIN_SIDES;
                    break;
                case SB_RIGHT:
                    pos = MAX_SIDES_SETTING;
                    bIncrement = TRUE;
                    break;
                default:
                    break;
                }
                if (pos < MIN_SIDES) {
                    pos = MIN_SIDES;
                }
                if (pos > MAX_SIDES_SETTING) {
                    pos = MAX_SIDES_SETTING;
                }
                if (pos == 2) {
                    pos = bIncrement ? 3 : 1;
                }
                SetScrollPos(hwndScroll, SB_CTL, pos, TRUE);
                SetSidesLabel(hDlg, pos);
                return 0;
            }

            if (hwndScroll == GetDlgItem(hDlg, IDC_SPEED)) {
                pos = GetScrollPos(hwndScroll, SB_CTL);
                switch (wParam) {
                case SB_LINELEFT:
                    pos -= 1;
                    break;
                case SB_LINERIGHT:
                    pos += 1;
                    break;
                case SB_PAGELEFT:
                    pos -= 10;
                    break;
                case SB_PAGERIGHT:
                    pos += 10;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    pos = (int) LOWORD(lParam);
                    break;
                case SB_LEFT:
                    pos = MIN_SPEED;
                    break;
                case SB_RIGHT:
                    pos = MAX_SPEED;
                    break;
                default:
                    break;
                }
                if (pos < MIN_SPEED) {
                    pos = MIN_SPEED;
                }
                if (pos > MAX_SPEED) {
                    pos = MAX_SPEED;
                }
                SetScrollPos(hwndScroll, SB_CTL, pos, TRUE);
                SetDlgItemInt(hDlg, IDC_SPEED_VALUE, (UINT) pos, FALSE);
                return 0;
            }

            if (hwndScroll == GetDlgItem(hDlg, IDC_TRAIL)) {
                pos = GetScrollPos(hwndScroll, SB_CTL);
                switch (wParam) {
                case SB_LINELEFT:
                    pos -= 1;
                    break;
                case SB_LINERIGHT:
                    pos += 1;
                    break;
                case SB_PAGELEFT:
                    pos -= 10;
                    break;
                case SB_PAGERIGHT:
                    pos += 10;
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    pos = (int) LOWORD(lParam);
                    break;
                case SB_LEFT:
                    pos = MIN_TRAIL;
                    break;
                case SB_RIGHT:
                    pos = MAX_TRAIL;
                    break;
                default:
                    break;
                }
                if (pos < MIN_TRAIL) {
                    pos = MIN_TRAIL;
                }
                if (pos > MAX_TRAIL) {
                    pos = MAX_TRAIL;
                }
                SetScrollPos(hwndScroll, SB_CTL, pos, TRUE);
                SetDlgItemInt(hDlg, IDC_TRAIL_VALUE, (UINT) pos, FALSE);
                return 0;
            }

            return FALSE;
        }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            gColorIndex = (int) SendDlgItemMessage(hDlg, IDC_COLOR, CB_GETCURSEL, 0, 0);
            if (gColorIndex < 0 || gColorIndex >= NUM_ITEMS) {
                gColorIndex = DEFAULT_COLOR;
            }
            gShapeSize = GetScrollPos(GetDlgItem(hDlg, IDC_BALLSIZE), SB_CTL);
            if (gShapeSize < MIN_SIZE) {
                gShapeSize = MIN_SIZE;
            }
            if (gShapeSize > MAX_SIZE) {
                gShapeSize = MAX_SIZE;
            }
            gSides = GetScrollPos(GetDlgItem(hDlg, IDC_SIDES), SB_CTL);
            if (gSides < MIN_SIDES) {
                gSides = MIN_SIDES;
            }
            if (gSides > MAX_SIDES_SETTING) {
                gSides = MAX_SIDES_SETTING;
            }
            if (gSides == 2) {
                gSides = 1;
            }
            gSpeed = GetScrollPos(GetDlgItem(hDlg, IDC_SPEED), SB_CTL);
            if (gSpeed < MIN_SPEED) {
                gSpeed = MIN_SPEED;
            }
            if (gSpeed > MAX_SPEED) {
                gSpeed = MAX_SPEED;
            }
            gTrailLength = GetScrollPos(GetDlgItem(hDlg, IDC_TRAIL), SB_CTL);
            if (gTrailLength < MIN_TRAIL) {
                gTrailLength = MIN_TRAIL;
            }
            if (gTrailLength > MAX_TRAIL) {
                gTrailLength = MAX_TRAIL;
            }
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

    gColorIndex = GetPrivateProfileInt(INI_SECTION, KEY_COLOR, DEFAULT_COLOR, INI_FILE);
    if (gColorIndex < 0 || gColorIndex >= NUM_ITEMS) {
        gColorIndex = DEFAULT_COLOR;
    }

    gShapeSize = GetPrivateProfileInt(INI_SECTION, KEY_SIZE, DEFAULT_SIZE, INI_FILE);
    if (gShapeSize < MIN_SIZE) {
        gShapeSize = MIN_SIZE;
    }
    if (gShapeSize > MAX_SIZE) {
        gShapeSize = MAX_SIZE;
    }

    gSides = GetPrivateProfileInt(INI_SECTION, KEY_SIDES, DEFAULT_SIDES, INI_FILE);
    if (gSides < MIN_SIDES) {
        gSides = MIN_SIDES;
    }
    if (gSides > MAX_SIDES_SETTING) {
        gSides = MAX_SIDES_SETTING;
    }
    if (gSides == 2) {
        gSides = 1;
    }

    if (gSides == IDX_SIDES_CYCLE) {
        gCurrentSides = MIN_SIDES;
    } else if (gSides == IDX_SIDES_RANDOM) {
        gCurrentSides = RandomValidSides();
    } else {
        gCurrentSides = gSides;
    }

    gTrailLength = GetPrivateProfileInt(INI_SECTION, KEY_TRAIL, DEFAULT_TRAIL, INI_FILE);
    if (gTrailLength < MIN_TRAIL) {
        gTrailLength = MIN_TRAIL;
    }
    if (gTrailLength > MAX_TRAIL) {
        gTrailLength = MAX_TRAIL;
    }
}

static void SaveSettings(void)
{
    char buf[16];
    sprintf(buf, "%d", gSpeed);
    WritePrivateProfileString(INI_SECTION, KEY_SPEED, buf, INI_FILE);
    sprintf(buf, "%d", gColorIndex);
    WritePrivateProfileString(INI_SECTION, KEY_COLOR, buf, INI_FILE);
    sprintf(buf, "%d", gShapeSize);
    WritePrivateProfileString(INI_SECTION, KEY_SIZE, buf, INI_FILE);
    sprintf(buf, "%d", gSides);
    WritePrivateProfileString(INI_SECTION, KEY_SIDES, buf, INI_FILE);
    sprintf(buf, "%d", gTrailLength);
    WritePrivateProfileString(INI_SECTION, KEY_TRAIL, buf, INI_FILE);
}

static void SetSidesLabel(HWND hDlg, int sides)
{
    char buf[24];
    if (sides > MAX_SIDES) {
        sprintf(buf, "%s", gShapeNames[sides]);
    } else {
        sprintf(buf, "%d: %s", sides, gShapeNames[sides]);
    }
    SetDlgItemText(hDlg, IDC_SIDES_VALUE, buf);
}

/* Next shape in Cycle mode: steps through every valid side count, skipping
   the degenerate 2 and wrapping from Pentadecagon back to Circle. */
static int NextValidSides(int sides)
{
    sides++;
    if (sides == 2) {
        sides = 3;
    }
    if (sides > MAX_SIDES) {
        sides = MIN_SIDES;
    }
    return sides;
}

/* A uniformly random shape for Random mode, from the 14 valid side counts
   {1,3,4,...,15}: index 0 maps to 1 (Circle), index n>0 maps to n+2. */
static int RandomValidSides(void)
{
    int idx = rand() % (MAX_SIDES - 1);
    return (idx == 0) ? MIN_SIDES : idx + 2;
}

static void ShapeRect(RECT FAR *rcClient, int x, int y, RECT FAR *rcOut)
{
    rcOut->left   = rcClient->left + x - SHAPE_MARGIN;
    rcOut->top    = rcClient->top + y - SHAPE_MARGIN;
    rcOut->right  = rcClient->left + x + gShapeSize + SHAPE_MARGIN;
    rcOut->bottom = rcClient->top + y + gShapeSize + SHAPE_MARGIN;
}

static void AdvanceAnimation(RECT FAR *rc)
{
    int width = rc->right - rc->left;
    int height = rc->bottom - rc->top;
    BOOL bBounced = FALSE;

    if (width <= gShapeSize || height <= gShapeSize) {
        return;
    }

    gX += gDX;
    gY += gDY;

    if (gX < 0) {
        gX = 0;
        gDX = -gDX;
        bBounced = TRUE;
    }
    if (gY < 0) {
        gY = 0;
        gDY = -gDY;
        bBounced = TRUE;
    }
    if (gX + gShapeSize > width) {
        gX = width - gShapeSize;
        gDX = -gDX;
        bBounced = TRUE;
    }
    if (gY + gShapeSize > height) {
        gY = height - gShapeSize;
        gDY = -gDY;
        bBounced = TRUE;
    }

    if (bBounced) {
        if (gColorIndex == IDX_RAINBOW) {
            gRainbowIndex = (gRainbowIndex + 1) % (int) NUM_COLORS;
        }
        if (gSides == IDX_SIDES_CYCLE) {
            gCurrentSides = NextValidSides(gCurrentSides);
        } else if (gSides == IDX_SIDES_RANDOM) {
            gCurrentSides = RandomValidSides();
        }
    }
}

static void DrawFrame(HDC hdc, RECT FAR *rc)
{
    int i;

    FillRect(hdc, rc, (HBRUSH) GetStockObject(BLACK_BRUSH));

    /* Oldest (dimmest) ghost first, so the brightest ghost and the live
       shape paint on top where they overlap. */
    for (i = 0; i < gTrailCount; i++) {
        int idx = (gTrailHead + gTrailLength - gTrailCount + i) % gTrailLength;
        int age = gTrailCount - i; /* i=0 -> oldest -> largest age */
        COLORREF dimmed = DimColor(gTrail[idx].color, age, gTrailCount);
        DrawShapeAt(hdc, rc->left + gTrail[idx].x, rc->top + gTrail[idx].y,
                    gTrail[idx].sides, dimmed);
    }

    DrawShapeAt(hdc, rc->left + gX, rc->top + gY, gCurrentSides, CurrentColor());
}

static void DrawShapeAt(HDC hdc, int left, int top, int sides, COLORREF color)
{
    HBRUSH hbrShape, hbrOld;

    hbrShape = CreateSolidBrush(color);
    hbrOld = SelectObject(hdc, hbrShape);

    if (sides <= 1) {
        Ellipse(hdc, left, top, left + gShapeSize, top + gShapeSize);
    } else {
        POINT pts[MAX_SIDES];
        double centerX = left + gShapeSize / 2.0;
        double centerY = top + gShapeSize / 2.0;
        double radius = gShapeSize / 2.0;
        double angleStep = 2.0 * PI / sides;
        /* Point-up looks right for every N-gon except a square, which
           reads to a human as a diamond unless rotated flat-top. */
        double startAngle = (sides == 4) ? (-PI / 2.0 + angleStep / 2.0) : (-PI / 2.0);
        int i;

        for (i = 0; i < sides; i++) {
            double angle = startAngle + i * angleStep;
            pts[i].x = (int) (centerX + radius * cos(angle));
            pts[i].y = (int) (centerY + radius * sin(angle));
        }
        Polygon(hdc, pts, sides);
    }

    SelectObject(hdc, hbrOld);
    DeleteObject(hbrShape);
}

static COLORREF CurrentColor(void)
{
    return (gColorIndex == IDX_RAINBOW) ? gColors[gRainbowIndex].color
                                         : gColors[gColorIndex].color;
}

/* Linearly interpolates color toward black. age 1 is the most recently
   pushed (brightest) ghost, age maxAge the oldest (dimmest). */
static COLORREF DimColor(COLORREF color, int age, int maxAge)
{
    double factor = (double) (maxAge - age + 1) / (maxAge + 1);
    BYTE r = (BYTE) (GetRValue(color) * factor);
    BYTE g = (BYTE) (GetGValue(color) * factor);
    BYTE b = (BYTE) (GetBValue(color) * factor);
    return RGB(r, g, b);
}

static void PushTrail(int x, int y, int sides, COLORREF color)
{
    if (gTrailLength <= 0) {
        return;
    }
    gTrail[gTrailHead].x = x;
    gTrail[gTrailHead].y = y;
    gTrail[gTrailHead].sides = sides;
    gTrail[gTrailHead].color = color;
    gTrailHead = (gTrailHead + 1) % gTrailLength;
    if (gTrailCount < gTrailLength) {
        gTrailCount++;
    }
}
