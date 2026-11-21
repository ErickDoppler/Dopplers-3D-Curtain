// Tool bar in the look of SFX-Parser: dark strip, rounded buttons, coloured vector icons drawn with GDI. The first five icons are the ones from SFX-Parser.
#include "toolbar.h"
#include <commctrl.h>
#include <cmath>
#include <vector>
#include "ids.h"

namespace {
enum Icon { I_OPEN, I_SAVE, I_SAVEAS, I_UNDO, I_REDO, I_DISTANCE, I_PRECISION, I_DENSITY, I_SIZE, I_ROTATE, I_MOVE, I_DETAILS, I_ERASER };
struct Btn { int id; Icon icon; const wchar_t* tip; bool tool, en, on; bool sepAfter; };
std::vector<Btn> g_b = {
    {IDM_OPEN, I_OPEN, L"Open a 3D model, a picture, or a project   Ctrl+O", false, true, false, false},
    {IDM_SAVE, I_SAVE, L"Save the project (.curtain)   Ctrl+S", false, false, false, false},
    {IDM_SAVEAS, I_SAVEAS, L"Save the project under a new name", false, false, false, true},
    {IDM_UNDO, I_UNDO, L"Undo   Ctrl+Z   (up to 200 steps)", false, false, false, false},
    {IDM_REDO, I_REDO, L"Redo   Ctrl+Y", false, false, false, true},
    {IDT_DISTANCE, I_DISTANCE, L"Curtain distance: how far the curtain is from the object. Shows the curtain in red with a slider and a number", true, false, false, false},
    {IDT_PRECISION, I_PRECISION, L"Curtain precision: how closely the wrap follows every curve. High = like a vinyl wrap, low = cuts the corners", true, false, false, false},
    {IDT_DENSITY, I_DENSITY, L"Wrap density: how many vertices the wrap has (does not change its shape)", true, false, false, true},
    {IDT_SCALE, I_SIZE, L"Object size: change the size separately per axis (handles in the 3D view, sliders below)", true, false, false, false},
    {IDT_ROTATE, I_ROTATE, L"Rotate the object relative to the horizon (rings in the 3D view)", true, false, false, false},
    {IDT_MOVE, I_MOVE, L"Move the object on the working plate (arrows in the 3D view, or drag the object)", true, false, false, true},
    {IDT_DETAILS, I_DETAILS, L"Object details: remove a share of the object's vertices (0 - 100 %, step 0.01 %) and press Apply", true, false, false, false},
    {IDT_ERASER, I_ERASER, L"Eraser: drag over the object to delete the vertices closest to your eye under the brush. The hole is closed with new ribs between the vertices that were connected to the deleted ones", true, false, false, false},
};
const int kBar = 48, kBtnW = 40, kBtnH = 38;
HWND g_bar = nullptr, g_tip = nullptr; int g_hover = -1, g_down = -1; bool g_tracking = false;

RECT btnRect(int i) {
    int x = 8; for (int k = 0; k < i; k++) x += kBtnW + 4 + (g_b[k].sepAfter ? 12 : 0);
    return RECT{x, 5, x + kBtnW, 5 + kBtnH};
}
int btnAt(int x, int y) { for (int i = 0; i < (int)g_b.size(); i++) { RECT r = btnRect(i); if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i; } return -1; }

void drawIcon(HDC dc, Icon ic, const RECT& r, COLORREF col, bool en) {
    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const COLORREF dark = RGB(GetRValue(col) * 5 / 10, GetGValue(col) * 5 / 10, GetBValue(col) * 5 / 10), dim = en ? RGB(128, 136, 150) : RGB(70, 74, 84);
    auto P = [&](int x, int y) { return POINT{cx + x, cy + y}; };
    auto poly = [&](std::vector<POINT> p, COLORREF c) {
        HBRUSH br = CreateSolidBrush(c); HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, p.data(), (int)p.size()); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(br); };
    auto rc = [&](int l, int t, int rr, int bb, COLORREF c) { RECT q{cx + l, cy + t, cx + rr, cy + bb}; HBRUSH br = CreateSolidBrush(c); FillRect(dc, &q, br); DeleteObject(br); };
    auto line = [&](int x1, int y1, int x2, int y2, int w, COLORREF c) { HPEN pen = CreatePen(PS_SOLID, w, c); HGDIOBJ op = SelectObject(dc, pen); MoveToEx(dc, cx + x1, cy + y1, nullptr); LineTo(dc, cx + x2, cy + y2); SelectObject(dc, op); DeleteObject(pen); };
    auto ell = [&](int l, int t, int rr, int bb, COLORREF fill, int pw, COLORREF pc, bool filled) {
        HBRUSH br = filled ? CreateSolidBrush(fill) : (HBRUSH)GetStockObject(NULL_BRUSH); HPEN pen = pw ? CreatePen(PS_SOLID, pw, pc) : (HPEN)GetStockObject(NULL_PEN);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen); Ellipse(dc, cx + l, cy + t, cx + rr, cy + bb); SelectObject(dc, op); SelectObject(dc, ob); if (filled) DeleteObject(br); if (pw) DeleteObject(pen); };
    auto dot = [&](int x, int y, COLORREF c) { ell(x - 2, y - 2, x + 3, y + 3, c, 0, 0, true); };
    auto arc = [&](double R, int a0, int a1, int sg, int w) {
        HPEN pen = CreatePen(PS_SOLID, w, col); HGDIOBJ op = SelectObject(dc, pen);
        for (int a = a0, first = 1; a <= a1; a += 10, first = 0) { const double t = a * 3.14159265358979 / 180; const int x = cx + (int)std::lround(sg * R * std::cos(t)), y = cy + 1 + (int)std::lround(R * std::sin(t)); if (first) MoveToEx(dc, x, y, nullptr); else LineTo(dc, x, y); }
        SelectObject(dc, op); DeleteObject(pen); };
    switch (ic) {
    case I_OPEN:                                                                                // --- SFX-Parser icons
        poly({P(-10, -8), P(-3, -8), P(-1, -5), P(9, -5), P(9, 8), P(-10, 8)}, dark);
        poly({P(-10, 8), P(-7, -2), P(12, -2), P(9, 8)}, col);
        break;
    case I_SAVE: case I_SAVEAS: {
        const int o = ic == I_SAVEAS ? -2 : 0;
        poly({P(o - 9, o - 9), P(o + 5, o - 9), P(o + 9, o - 5), P(o + 9, o + 9), P(o - 9, o + 9)}, col);
        rc(o - 5, o - 9, o + 4, o - 3, RGB(28, 32, 42)); rc(o + 1, o - 8, o + 3, o - 4, col);
        rc(o - 6, o + 1, o + 6, o + 9, RGB(236, 241, 250)); rc(o - 4, o + 4, o + 4, o + 5, dark); rc(o - 4, o + 6, o + 2, o + 7, dark);
        if (ic == I_SAVEAS) { HPEN pen = CreatePen(PS_SOLID, 4, en ? RGB(255, 214, 80) : dim); HGDIOBJ op = SelectObject(dc, pen); MoveToEx(dc, cx + 4, cy + 9, nullptr); LineTo(dc, cx + 10, cy + 3); SelectObject(dc, op); DeleteObject(pen);
                              poly({P(1, 13), P(2, 8), P(6, 12)}, RGB(250, 250, 250)); }
        break; }
    case I_UNDO: case I_REDO: {
        const int sg = ic == I_UNDO ? 1 : -1; arc(7.5, 270, 500, sg, 3);
        poly({P(-8 * sg, -6), P(sg, -12), P(sg, 0)}, col);
        break; }
    case I_DISTANCE:                                                                            // --- new icons, same style: the object, the curtain around it, and the gap
        ell(-4, -4, 5, 5, RGB(200, 205, 215), 0, 0, true); if (!en) ell(-4, -4, 5, 5, dim, 0, 0, true);
        ell(-11, -11, 12, 12, 0, 2, col, false); line(3, -3, 8, -8, 2, en ? RGB(240, 244, 250) : dim); poly({P(11, -11), P(4, -9), P(9, -4)}, en ? RGB(240, 244, 250) : dim);
        break;
    case I_PRECISION:                                                                           // a stepped body and the wrap that follows it
        line(-10, 8, -4, 8, 2, dim); line(-4, 8, -4, 1, 2, dim); line(-4, 1, 3, 1, 2, dim); line(3, 1, 3, -6, 2, dim); line(3, -6, 10, -6, 2, dim);
        line(-10, 4, -7, 4, 2, col); line(-7, 4, -7, -3, 2, col); line(-7, -3, 0, -3, 2, col); line(0, -3, 0, -10, 2, col); line(0, -10, 10, -10, 2, col);
        dot(-7, 4, col); dot(-7, -3, col); dot(0, -3, col); dot(0, -10, col);
        break;
    case I_DENSITY:                                                                             // a little mesh with its vertices
        line(-9, 6, 9, 6, 1, dark); line(-9, 6, -4, -1, 1, dark); line(-4, -1, 0, 6, 1, dark); line(0, 6, 5, -1, 1, dark); line(5, -1, 9, 6, 1, dark); line(-4, -1, 5, -1, 1, dark); line(-4, -1, 0, -8, 1, dark); line(0, -8, 5, -1, 1, dark);
        for (POINT p : {POINT{-9, 6}, POINT{0, 6}, POINT{9, 6}, POINT{-4, -1}, POINT{5, -1}, POINT{0, -8}}) dot(p.x, p.y, col);
        break;
    case I_SIZE:                                                                                // a box growing along the diagonal
        line(-10, -2, 3, -2, 2, col); line(3, -2, 3, 11, 2, col); line(3, 11, -10, 11, 2, col); line(-10, 11, -10, -2, 2, col);
        line(-1, 7, 7, -1, 2, col); poly({P(12, -12), P(4, -10), P(10, -4)}, col);
        break;
    case I_ROTATE: arc(8, 40, 330, 1, 3); poly({P(1, -12), P(7, -6), P(-1, -5)}, col); break;
    case I_MOVE:                                                                                // four arrows
        line(-9, 0, 9, 0, 2, col); line(0, -9, 0, 9, 2, col);
        poly({P(-12, 0), P(-6, -4), P(-6, 4)}, col); poly({P(12, 0), P(6, -4), P(6, 4)}, col); poly({P(0, -12), P(-4, -6), P(4, -6)}, col); poly({P(0, 12), P(-4, 6), P(4, 6)}, col);
        break;
    case I_ERASER: {                                                                            // a slanted eraser: white top, pink end, and the crumbs it leaves
        poly({P(-3, -9), P(11, -2), P(8, 4), P(-6, -3)}, en ? RGB(240, 244, 250) : dim);
        poly({P(-6, -3), P(8, 4), P(5, 10), P(-9, 3)}, col);
        line(-12, 12, -4, 12, 2, dim); dot(-1, 13, dim); dot(5, 14, dim);
        break; }
    case I_DETAILS:                                                                             // cubes of the grid: some stay, some are removed
        for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) {
            const int x = i * 8, y = j * 8; if (((i + j) & 1) == 0) rc(x - 3, y - 3, x + 4, y + 4, col);
            else { line(x - 3, y - 3, x + 3, y - 3, 1, dim); line(x + 3, y - 3, x + 3, y + 3, 1, dim); line(x + 3, y + 3, x - 3, y + 3, 1, dim); line(x - 3, y + 3, x - 3, y - 3, 1, dim); }
        }
        break;
    }
}
COLORREF iconColor(Icon ic) {
    switch (ic) {
    case I_OPEN: return RGB(255, 196, 80); case I_SAVE: case I_SAVEAS: return RGB(110, 190, 245); case I_UNDO: case I_REDO: return RGB(190, 160, 255);
    case I_DISTANCE: return RGB(255, 120, 100); case I_PRECISION: return RGB(110, 225, 200); case I_DENSITY: return RGB(120, 170, 255);
    case I_ERASER: return RGB(255, 140, 170); case I_SIZE: return RGB(110, 230, 140); case I_ROTATE: return RGB(100, 215, 245); case I_MOVE: return RGB(255, 214, 80); case I_DETAILS: return RGB(255, 130, 190);
    }
    return RGB(225, 230, 240);
}

void paint(HWND h) {
    PAINTSTRUCT ps; HDC out = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(out); HBITMAP bm = CreateCompatibleBitmap(out, cr.right, cr.bottom); HGDIOBJ old = SelectObject(dc, bm);
    HBRUSH bg = CreateSolidBrush(RGB(30, 32, 37)); FillRect(dc, &cr, bg); DeleteObject(bg);
    for (int i = 0; i < (int)g_b.size(); i++) {
        const Btn& b = g_b[i]; const RECT br = btnRect(i); const bool hov = g_hover == i && b.en, down = g_down == i && b.en;
        HBRUSH bb = CreateSolidBrush(!b.en ? RGB(36, 39, 45) : (b.on ? RGB(40, 84, 60) : (down ? RGB(84, 94, 118) : (hov ? RGB(66, 74, 92) : RGB(46, 50, 60)))));
        HGDIOBJ ob = SelectObject(dc, bb), op = SelectObject(dc, GetStockObject(NULL_PEN)); RoundRect(dc, br.left, br.top, br.right + 1, br.bottom + 1, 9, 9);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(bb);
        drawIcon(dc, b.icon, br, b.en ? iconColor(b.icon) : RGB(88, 92, 102), b.en);
        if (b.sepAfter) { HPEN sp = CreatePen(PS_SOLID, 1, RGB(64, 68, 80)); HGDIOBJ o = SelectObject(dc, sp); MoveToEx(dc, br.right + 8, 10, nullptr); LineTo(dc, br.right + 8, kBar - 10); SelectObject(dc, o); DeleteObject(sp); }
    }
    BitBlt(out, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
}

LRESULT CALLBACK barProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        const int i = btnAt((short)LOWORD(lp), (short)HIWORD(lp));
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_tracking = true; }
        if (i != g_hover) { g_hover = i; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g_tracking = false; g_hover = -1; g_down = -1; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: { g_down = btnAt((short)LOWORD(lp), (short)HIWORD(lp)); if (g_down >= 0) SetCapture(h); InvalidateRect(h, nullptr, FALSE); return 0; }
    case WM_LBUTTONUP: {
        const int d = g_down; g_down = -1; ReleaseCapture(); InvalidateRect(h, nullptr, FALSE);
        if (d >= 0 && d == btnAt((short)LOWORD(lp), (short)HIWORD(lp)) && g_b[d].en) SendMessageW(GetParent(h), WM_COMMAND, g_b[d].id, 0);
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}
}  // namespace

int toolbarHeight() { return kBar; }
void toolbarEnable(int id, bool on) { for (auto& b : g_b) if (b.id == id && b.en != on) { b.en = on; if (g_bar) InvalidateRect(g_bar, nullptr, FALSE); } }
void toolbarCheck(int id, bool on) { for (auto& b : g_b) if (b.id == id && b.on != on) { b.on = on; if (g_bar) InvalidateRect(g_bar, nullptr, FALSE); } }

HWND createToolbar(HWND parent, HINSTANCE inst) {
    WNDCLASSW wc = {}; wc.lpfnWndProc = barProc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"D3CBar"; RegisterClassW(&wc);
    g_bar = CreateWindowExW(0, L"D3CBar", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, kBar, parent, (HMENU)(INT_PTR)IDC_TOOLBAR, inst, nullptr);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, parent, nullptr, inst, nullptr);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 420); SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 250); SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);
    for (int i = 0; i < (int)g_b.size(); i++) {
        TOOLINFOW ti = {sizeof ti}; ti.uFlags = TTF_SUBCLASS; ti.hwnd = g_bar; ti.uId = (UINT_PTR)i; ti.rect = btnRect(i); ti.lpszText = (LPWSTR)g_b[i].tip;
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    return g_bar;
}
