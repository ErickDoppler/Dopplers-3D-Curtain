// Doppler's 3D Curtain - main window: menu, tool bar, tabs, the tool panel over the 3D view, image tabs and the shared queue.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include "app.h"
#include "download.h"
#include "h3d/pipeline.h"
#include "ids.h"
#include "project.h"
#include "toolbar.h"
#include "sys.h"

void setUniformScale(bool on);

// ---------------------------------------------------------------------------------------------------------------- state
namespace {
const UINT WM_CURTAIN_DONE = WM_APP + 10, WM_JOB_UPDATE = WM_APP + 11, WM_JOB_DONE = WM_APP + 12;
const int TIMER_CURTAIN = 1;

HINSTANCE g_inst; HWND g_main, g_tab, g_toolbar, g_status, g_view, g_panel, g_welcome, g_queue, g_imgPanel;
HFONT g_font, g_bold;
std::vector<std::unique_ptr<Doc>> g_docs; int g_active = -1; uint64_t g_nextId = 1;
Tool g_tool = Tool::None;
bool g_syncing = false;
std::shared_ptr<std::atomic<bool>> g_curCancel;

struct Panel {
    HWND title, label[3], tb[3], ed[3], apply, uniform, mode;
    int rows = 0; double mn[3], mx[3], factor[3]; int dec[3]; bool logScale[3] = {false, false, false};
} P;
HWND g_hide;
HWND g_viewBtn[7], g_preview, g_alpha;

// image tab controls
HWND g_imgLabel[5], g_imgEdit[5], g_imgConvert, g_imgRandom, g_imgInfo;

struct Job { uint64_t id; std::string name; std::string state; int percent = 0; };
std::vector<Job> g_jobs; std::mutex g_jobMx; uint64_t g_nextJob = 1;
}  // namespace

HFONT appFont() { return g_font; }
const char* APP_NAME() { return "Doppler's 3D Curtain"; }
ModelDoc* activeModel() { return (g_active >= 0 && g_active < (int)g_docs.size() && g_docs[g_active]->kind == Doc::Model) ? static_cast<ModelDoc*>(g_docs[g_active].get()) : nullptr; }
static ImageDoc* activeImage() { return (g_active >= 0 && g_active < (int)g_docs.size() && g_docs[g_active]->kind == Doc::Image) ? static_cast<ImageDoc*>(g_docs[g_active].get()) : nullptr; }
Tool activeTool() { return activeModel() ? g_tool : Tool::None; }

static std::wstring W(const std::string& s) { return sys::widen(s); }
static void msg(const std::string& text, UINT icon = MB_ICONINFORMATION) { MessageBoxW(g_main, W(text).c_str(), L"Doppler's 3D Curtain", MB_OK | icon); }
static std::string baseName(const std::string& p) { size_t k = p.find_last_of("\\/"); return k == std::string::npos ? p : p.substr(k + 1); }
static std::string extOf(const std::string& p) { size_t k = p.find_last_of('.'); std::string e = k == std::string::npos ? "" : p.substr(k + 1); for (auto& c : e) c = (char)tolower((unsigned char)c); return e; }
static std::string withExt(const std::string& p, const std::string& e) { size_t k = p.find_last_of('.'), s = p.find_last_of("\\/"); return (k == std::string::npos || (s != std::string::npos && k < s)) ? p + "." + e : p.substr(0, k + 1) + e; }

// ---------------------------------------------------------------------------------------------------------------- curtain jobs
bool curtainWanted(ModelDoc* d) { return d && d->cur.mesh && (d->showCurtain || g_tool == Tool::Distance || g_tool == Tool::Precision || g_tool == Tool::Density); }
static bool curtainStale(ModelDoc* d) { return !d->curtain || d->curtainGeomVer != d->geomVer || !(d->curtainParams == d->cur.cp); }

struct CurJob { uint64_t docId, geomVer; CurtainParams p; std::shared_ptr<CurtainResult> res; };
void requestCurtain(ModelDoc* d) {
    if (!d || !d->cur.mesh || !curtainStale(d)) return;
    if (g_curCancel) *g_curCancel = true;
    auto cancel = std::make_shared<std::atomic<bool>>(false); g_curCancel = cancel; d->curtainBusy = true;
    std::vector<Vec3> wv = d->world; MeshPtr mesh = d->cur.mesh; const uint64_t id = d->id, gv = d->geomVer; const CurtainParams p = d->cur.cp; HWND hw = g_main;
    std::thread([=] {
        auto r = std::make_shared<CurtainResult>(); bool ok = computeCurtain(wv, mesh->idx, p, *r, cancel.get());
        if (ok && !cancel->load()) PostMessageW(hw, WM_CURTAIN_DONE, 0, (LPARAM) new CurJob{id, gv, p, r});
    }).detach();
}
static void ensureCurtainNow(ModelDoc* d) {                                              // synchronous, for exports
    if (!curtainStale(d)) return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    auto r = std::make_shared<CurtainResult>();
    if (computeCurtain(d->world, d->cur.mesh->idx, d->cur.cp, *r, nullptr)) { d->curtain = r; d->curtainGeomVer = d->geomVer; d->curtainParams = d->cur.cp; }
}

// ---------------------------------------------------------------------------------------------------------------- UI helpers
static std::string fmtCount(size_t n) { std::string s = std::to_string(n), o; int c = 0; for (int i = (int)s.size() - 1; i >= 0; i--) { o.insert(o.begin(), s[i]); if (++c % 3 == 0 && i) o.insert(o.begin(), ' '); } return o; }
static void setStatusText(const std::string& s) { SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)W(s).c_str()); }
static void updateStatus() {
    ModelDoc* d = activeModel(); ImageDoc* im = activeImage();
    if (d && d->cur.mesh) {
        char b[400]; Vec3 s = d->whi - d->wlo;
        std::string t = "Vertices " + fmtCount(d->cur.mesh->v.size()) + "   Triangles " + fmtCount(d->cur.mesh->triCount());
        snprintf(b, sizeof b, "   Size %.3g x %.3g x %.3g", s.x, s.y, s.z); t += b;
        if (d->curtainBusy) t += "   Curtain: computing...";
        else if (d->curtain && !curtainStale(d)) { snprintf(b, sizeof b, "   Curtain: grid %dx%dx%d, wrap %s vertices %s triangles, %.2f s", d->curtain->field.n[0], d->curtain->field.n[1], d->curtain->field.n[2], fmtCount(d->curtain->v.size()).c_str(), fmtCount(d->curtain->idx.size() / 3).c_str(), d->curtain->seconds); t += b; }
        setStatusText(t);
    } else if (im) setStatusText("Image " + std::to_string(im->w) + " x " + std::to_string(im->h) + " - set the options and press Convert");
    else setStatusText("Open a 3D model (OBJ, STL, PLY, glTF/GLB) or an image: File > Open, or drop a file on the window.");
}

static void refreshTabs() {
    TabCtrl_DeleteAllItems(g_tab);
    for (size_t i = 0; i < g_docs.size(); i++) {
        std::wstring t = W(g_docs[i]->title + (g_docs[i]->dirty ? " *" : "")); TCITEMW it = {}; it.mask = TCIF_TEXT; it.pszText = &t[0]; TabCtrl_InsertItem(g_tab, (int)i, &it);
    }
    if (g_active >= 0) TabCtrl_SetCurSel(g_tab, g_active);
}
static void refreshCommands() {
    ModelDoc* d = activeModel(); HMENU m = GetMenu(g_main);
    const bool can = d != nullptr, un = can && !d->undo.empty(), re = can && !d->redo.empty();
    toolbarEnable(IDM_UNDO, un); toolbarEnable(IDM_REDO, re);
    for (int id : {IDM_SAVE, IDM_SAVEAS}) toolbarEnable(id, can);
    for (int id : {IDT_DISTANCE, IDT_PRECISION, IDT_DENSITY, IDT_SCALE, IDT_ROTATE, IDT_MOVE, IDT_DETAILS, IDT_ERASER}) toolbarEnable(id, can);
    EnableMenuItem(m, IDM_UNDO, MF_BYCOMMAND | (un ? MF_ENABLED : MF_GRAYED)); EnableMenuItem(m, IDM_REDO, MF_BYCOMMAND | (re ? MF_ENABLED : MF_GRAYED));
    for (int id : {IDM_SAVE, IDM_SAVEAS, IDM_EXP_HEADER, IDM_EXP_BLOB, IDM_EXP_CURTAIN_OBJ, IDM_EXP_OBJ, IDM_EXP_GLB, IDM_CENTER, IDM_RESET, IDM_MODE_SMOOTH, IDM_MODE_HULL, IDM_VERTICES, IDM_PREVIEW, IDM_HIDE})
        EnableMenuItem(m, id, MF_BYCOMMAND | (can ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(m, IDM_MODE_SMOOTH, MF_BYCOMMAND | ((can && d->cur.cp.mode == CurtainMode::Smooth) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_MODE_HULL, MF_BYCOMMAND | ((can && d->cur.cp.mode == CurtainMode::Hull) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_VERTICES, MF_BYCOMMAND | ((can && d->showVertices) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_PREVIEW, MF_BYCOMMAND | ((can && d->showCurtain) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_HIDE, MF_BYCOMMAND | ((can && d->hideObject) ? MF_CHECKED : MF_UNCHECKED));
    const int ids[] = {IDT_DISTANCE, IDT_PRECISION, IDT_DENSITY, IDT_SCALE, IDT_ROTATE, IDT_MOVE, IDT_DETAILS, IDT_ERASER}; const Tool ts[] = {Tool::Distance, Tool::Precision, Tool::Density, Tool::Scale, Tool::Rotate, Tool::Move, Tool::Details, Tool::Eraser};
    for (int i = 0; i < 8; i++) toolbarCheck(ids[i], can && g_tool == ts[i]);
    if (g_hide) SendMessageW(g_hide, BM_SETCHECK, (can && d->hideObject) ? BST_CHECKED : BST_UNCHECKED, 0);
    g_syncing = true; if (g_preview) SendMessageW(g_preview, BM_SETCHECK, (can && d->showCurtain) ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_alpha && can) SendMessageW(g_alpha, TBM_SETPOS, TRUE, (LPARAM)std::lround((1.0f - d->alpha) * 100)); g_syncing = false;
}

// ---------------------------------------------------------------------------------------------------------------- tool panel
static const wchar_t* kModeText[] = {L"Smooth - sphere limit", L"Convex hull - distance"};
static int tbFromValue(int r, double v) { if (P.logScale[r]) return (int)std::lround(1000.0 * std::log10(std::max(v, P.mn[r]) / P.mn[r]) / std::log10(P.mx[r] / P.mn[r])); return (int)std::lround(v * P.factor[r]); }
static double valueFromTb(int r, int pos) { if (P.logScale[r]) return P.mn[r] * std::pow(P.mx[r] / P.mn[r], pos / 1000.0); return pos / P.factor[r]; }
static void fmtEdit(HWND e, double v, int dec) { wchar_t b[40]; swprintf(b, 40, L"%.*f", dec, v); SetWindowTextW(e, b); }
static double rowValue(ModelDoc* d, int r) {
    switch (g_tool) {
    case Tool::Distance: return d->cur.cp.distance * 100.0;
    case Tool::Precision: return d->cur.cp.precision;
    case Tool::Density: return d->cur.cp.density;
    case Tool::Eraser: return viewBrush();
    case Tool::Scale: return d->cur.xf.scale[r] * 100.0;
    case Tool::Rotate: return d->cur.xf.rotDeg[r];
    case Tool::Move: return r == 0 ? d->cur.xf.pos.x : d->cur.xf.pos.y;
    default: return 100.0;
    }
}
static void panelSync() {                                                          // from the document to the controls
    ModelDoc* d = activeModel(); if (!d || P.rows == 0) return;
    g_syncing = true;
    for (int r = 0; r < P.rows; r++) {
        double v = g_tool == Tool::Details ? (double)SendMessageW(P.tb[0], TBM_GETPOS, 0, 0) / P.factor[0] : rowValue(d, r);
        if (g_tool != Tool::Details) SendMessageW(P.tb[r], TBM_SETPOS, TRUE, (LPARAM)tbFromValue(r, v));
        if (g_tool != Tool::Details) fmtEdit(P.ed[r], v, P.dec[r]);
    }
    if (g_tool == Tool::Distance) SendMessageW(P.mode, CB_SETCURSEL, d->cur.cp.mode == CurtainMode::Hull ? 1 : 0, 0);
    g_syncing = false;
}
static void panelConfigure() {
    ModelDoc* d = activeModel(); const bool show = d && g_tool != Tool::None;
    ShowWindow(g_panel, show ? SW_SHOW : SW_HIDE); if (!show) { P.rows = 0; return; }
    const wchar_t* title = L""; const wchar_t* lab[3] = {L"", L"", L""}; P.rows = 1; for (bool& b : P.logScale) b = false;
    auto row = [&](int r, double mn, double mx, double f, int dec) { P.mn[r] = mn; P.mx[r] = mx; P.factor[r] = f; P.dec[r] = dec; };
    switch (g_tool) {
    case Tool::Distance: title = L"Curtain distance from the object (percent of the object size)"; lab[0] = L"Distance %"; row(0, 0, 150, 10, 1); break;
    case Tool::Precision: title = L"Precision: high = a wrap that follows every curve, low = cuts the corners"; lab[0] = L"Precision"; row(0, kMinPrecision, kMaxPrecision, 1, 0); break;
    case Tool::Density: title = L"Wrap density: number of vertices in the wrap (adds or removes vertices, the shape stays)"; lab[0] = L"Vertices"; row(0, kMinDensity, kMaxDensity, 1, 0); P.logScale[0] = true; break;
    case Tool::Scale: title = L"Object size per axis (percent)"; P.rows = 3; lab[0] = L"X %"; lab[1] = L"Y %"; lab[2] = L"Z %"; for (int r = 0; r < 3; r++) row(r, 5, 1000, 1, 1); break;
    case Tool::Rotate: title = L"Rotate the object about the horizon (degrees)"; P.rows = 3; lab[0] = L"X °"; lab[1] = L"Y °"; lab[2] = L"Z °"; for (int r = 0; r < 3; r++) row(r, -180, 180, 1, 1); break;
    case Tool::Move: { title = L"Move the object on the plate"; P.rows = 2; lab[0] = L"X"; lab[1] = L"Y"; double R = std::max(10.0 * d->diag(), 1.0); for (int r = 0; r < 2; r++) row(r, -R, R, 1000.0 / R, 3); break; }
    case Tool::Eraser: title = L"Eraser: drag over the object; the vertices nearest your eye go, the hole is closed"; lab[0] = L"Brush px"; row(0, 3, 300, 1, 0); break;
    case Tool::Details: title = L"Object details: share of the vertices that stay (0.01 % steps). Apply removes the rest and closes the holes"; lab[0] = L"Quality %"; row(0, 0, 100, 100, 2); break;
    default: break;
    }
    SetWindowTextW(P.title, title);
    for (int r = 0; r < 3; r++) {
        const bool on = r < P.rows; ShowWindow(P.label[r], on ? SW_SHOW : SW_HIDE); ShowWindow(P.tb[r], on ? SW_SHOW : SW_HIDE); ShowWindow(P.ed[r], on ? SW_SHOW : SW_HIDE);
        if (on) { SetWindowTextW(P.label[r], lab[r]);
                  const int lo = P.logScale[r] ? 0 : (int)std::lround(P.mn[r] * P.factor[r]), hi = P.logScale[r] ? 1000 : (int)std::lround(P.mx[r] * P.factor[r]);
                  SendMessageW(P.tb[r], TBM_SETRANGE, TRUE, MAKELONG(lo, hi)); SendMessageW(P.tb[r], TBM_SETTICFREQ, (WPARAM)std::max(1, (hi - lo) / 20), 0); }
    }
    ShowWindow(P.apply, g_tool == Tool::Details ? SW_SHOW : SW_HIDE); ShowWindow(P.uniform, g_tool == Tool::Scale ? SW_SHOW : SW_HIDE); ShowWindow(P.mode, g_tool == Tool::Distance ? SW_SHOW : SW_HIDE);
    if (g_tool == Tool::Details) { g_syncing = true; SendMessageW(P.tb[0], TBM_SETPOS, TRUE, 10000); fmtEdit(P.ed[0], 100.0, 2); g_syncing = false; }
    else panelSync();
    // size and position
    RECT rc; GetClientRect(g_main, &rc); RECT cr; GetWindowRect(g_view, &cr); POINT tl = {cr.left, cr.top}; ScreenToClient(g_main, &tl);
    const int w = 640, h = 30 + P.rows * 32 + 10, x = tl.x + (cr.right - cr.left - w) / 2, y = tl.y + (cr.bottom - cr.top) - h - 12;
    SetWindowPos(g_panel, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW); RedrawWindow(g_panel, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    SetWindowPos(P.title, nullptr, 12, 6, w - 24, 20, SWP_NOZORDER);
    for (int r = 0; r < P.rows; r++) {
        int ry = 30 + r * 32; SetWindowPos(P.label[r], nullptr, 12, ry + 4, 70, 20, SWP_NOZORDER); SetWindowPos(P.tb[r], nullptr, 84, ry, 380, 28, SWP_NOZORDER); SetWindowPos(P.ed[r], nullptr, 472, ry + 1, 80, 24, SWP_NOZORDER);
    }
    SetWindowPos(P.apply, nullptr, 562, 30, 66, 26, SWP_NOZORDER); SetWindowPos(P.uniform, nullptr, 562, 34, 74, 22, SWP_NOZORDER); SetWindowPos(P.mode, nullptr, 472, 58, 156, 120, SWP_NOZORDER);
    if (g_tool == Tool::Distance) { SetWindowPos(P.mode, nullptr, 84, 30 + 32, 200, 120, SWP_NOZORDER); SetWindowPos(g_panel, HWND_TOP, x, y - 34, w, h + 34, SWP_SHOWWINDOW); }
}

static void onGeometry(ModelDoc* d);
static void applyDetails(double q);
static void rowChanged(int r, double v, bool commit) {                             // a trackbar or edit changed row r
    ModelDoc* d = activeModel(); if (!d || !d->cur.mesh) return;
    v = std::min(P.mx[r], std::max(P.mn[r], v));
    if (g_tool == Tool::Eraser) {                                                                                  // the brush size is not part of the document
        viewSetBrush((int)std::lround(v)); g_syncing = true; SendMessageW(P.tb[0], TBM_SETPOS, TRUE, (LPARAM)tbFromValue(0, v)); fmtEdit(P.ed[0], v, 0); g_syncing = false; viewRedraw(); return;
    }
    d->beginEdit();
    bool geom = false, cparam = false; Xform& xf = d->cur.xf;
    switch (g_tool) {
    case Tool::Distance: d->cur.cp.distance = (float)(v / 100.0); cparam = true; break;
    case Tool::Precision: d->cur.cp.precision = (int)std::lround(v); cparam = true; break;
    case Tool::Density: d->cur.cp.density = (int)std::lround(v); cparam = true; break;
    case Tool::Scale: {
        const float nv = (float)(v / 100.0), old = xf.scale[r];
        BOOL uni = SendMessageW(P.uniform, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (uni && old > 0) xf.scale = xf.scale * (nv / old); else xf.scale[r] = nv;
        geom = true; break; }
    case Tool::Rotate: xf.rotDeg[r] = (float)v; geom = true; break;
    case Tool::Move: if (r == 0) xf.pos.x = (float)v; else xf.pos.y = (float)v; geom = true; break;
    default: return;
    }
    if (geom) { d->rebuild(); d->dropToPlate(); }
    if (g_tool != Tool::Details) { g_syncing = true; for (int k = 0; k < P.rows; k++) { double val = rowValue(d, k); if (k != r || commit) { SendMessageW(P.tb[k], TBM_SETPOS, TRUE, (LPARAM)tbFromValue(k, val)); fmtEdit(P.ed[k], val, P.dec[k]); } else fmtEdit(P.ed[r], val, P.dec[r]); } g_syncing = false; }
    if (commit) { d->endEdit(); refreshCommands(); refreshTabs(); }
    if (geom) onGeometry(d);
    if (cparam) { if (commit) requestCurtain(d); else SetTimer(g_main, TIMER_CURTAIN, 120, nullptr); }
    updateStatus(); viewRedraw();
}

// ---------------------------------------------------------------------------------------------------------------- hooks from the view
void onEditChanged(ModelDoc* d, bool commit) {
    if (d != activeModel()) return;
    panelSync(); onGeometry(d);
    if (commit) { refreshCommands(); refreshTabs(); if (curtainWanted(d)) requestCurtain(d); }
    updateStatus();
}
void onGeometryChanged(ModelDoc* d) { onGeometry(d); }
static void onGeometry(ModelDoc*) {}                                              // the curtain is requested when an edit is committed

static void setTool(Tool t) {
    g_tool = (g_tool == t) ? Tool::None : t;
    panelConfigure(); refreshCommands();
    if (ModelDoc* d = activeModel()) if (curtainWanted(d)) requestCurtain(d);
    viewRedraw();
}

static void applyDetails(double q) {
    ModelDoc* d = activeModel(); if (!d || !d->cur.mesh) return;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT)); d->beginEdit();
    auto m = reduceDetails(*d->cur.mesh, q);
    if (m->idx.empty()) { d->editing = false; msg("Nothing would be left at that quality. Use a higher value."); return; }
    Mat4 M = xformMatrix(d->cur.xf, d->pivot); Vec3 lo, hi; m->bounds(lo, hi); Vec3 np = (lo + hi) * 0.5f;
    d->cur.xf.pos = M.point(np); d->pivot = np; d->cur.mesh = m;                  // the shape stays where it is
    d->rebuild(); d->endEdit(); refreshCommands(); refreshTabs(); updateStatus();
    g_syncing = true; SendMessageW(P.tb[0], TBM_SETPOS, TRUE, 10000); fmtEdit(P.ed[0], 100.0, 2); g_syncing = false;
    if (curtainWanted(d)) requestCurtain(d);
    viewRedraw();
}

// ---------------------------------------------------------------------------------------------------------------- documents
static void layout();
static void showActive() {
    ModelDoc* d = activeModel(); ImageDoc* im = activeImage();
    ShowWindow(g_view, (d || (!im)) ? SW_SHOW : SW_HIDE); ShowWindow(g_imgPanel, im ? SW_SHOW : SW_HIDE);
    ShowWindow(g_welcome, (g_docs.empty()) ? SW_SHOW : SW_HIDE);
    for (HWND h : g_viewBtn) ShowWindow(h, d ? SW_SHOW : SW_HIDE);
    ShowWindow(g_preview, d ? SW_SHOW : SW_HIDE); ShowWindow(g_hide, d ? SW_SHOW : SW_HIDE); ShowWindow(g_alpha, (d && d->showCurtain) ? SW_SHOW : SW_HIDE);
    if (!d) ShowWindow(g_panel, SW_HIDE);
    for (int i = 0; i < 5; i++) { ShowWindow(g_imgLabel[i], im ? SW_SHOW : SW_HIDE); ShowWindow(g_imgEdit[i], im ? SW_SHOW : SW_HIDE); }
    for (HWND hh : {g_imgRandom, g_imgConvert, g_imgInfo}) ShowWindow(hh, im ? SW_SHOW : SW_HIDE);
    if (im) {
        wchar_t b[40]; swprintf(b, 40, L"%d", im->seed); SetWindowTextW(g_imgEdit[0], b); swprintf(b, 40, L"%d", im->steps); SetWindowTextW(g_imgEdit[1], b);
        swprintf(b, 40, L"%.1f", im->cfg); SetWindowTextW(g_imgEdit[2], b); swprintf(b, 40, L"%d", im->resolution); SetWindowTextW(g_imgEdit[3], b); swprintf(b, 40, L"%.2f", im->threshold); SetWindowTextW(g_imgEdit[4], b);
        SendMessageW(g_imgRandom, BM_SETCHECK, im->randomSeed ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(g_imgPanel, nullptr, TRUE);
    }
    layout(); panelConfigure(); refreshCommands(); updateStatus(); viewRedraw();
    if (d && curtainWanted(d)) requestCurtain(d);
}
static void selectDoc(int i) { g_active = i; if (i >= 0) TabCtrl_SetCurSel(g_tab, i); showActive(); }
static void addDoc(std::unique_ptr<Doc> d) { d->id = g_nextId++; g_docs.push_back(std::move(d)); refreshTabs(); selectDoc((int)g_docs.size() - 1); }

static HBITMAP loadBitmap(const std::string& path, int& w, int& h) {
    HBITMAP out = nullptr; CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* f = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) {
        IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
        if (SUCCEEDED(f->CreateDecoderFromFilename(W(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv))
            && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            UINT uw, uh; cv->GetSize(&uw, &uh); w = (int)uw; h = (int)uh;
            BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr; out = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (out) cv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits);
        }
        if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); f->Release();
    }
    return out;
}

static bool saveCurtainFile(ModelDoc* d, const std::string& path);
static bool loadCurtainFile(const std::string& path, std::unique_ptr<ModelDoc>& out, std::string& err);

static void openPath(const std::string& path) {
    for (size_t i = 0; i < g_docs.size(); i++) if (g_docs[i]->path == path) { selectDoc((int)i); return; }
    const std::string e = extOf(path);
    if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "tif" || e == "tiff" || e == "gif") {
        auto im = std::make_unique<ImageDoc>(); im->bmp = loadBitmap(path, im->w, im->h);
        if (!im->bmp) { msg("Cannot read the image " + path, MB_ICONERROR); return; }
        im->path = path; im->title = baseName(path); addDoc(std::move(im)); return;
    }
    SetCursor(LoadCursorW(nullptr, IDC_WAIT)); std::string err;
    if (e == "curtain") {
        std::unique_ptr<ModelDoc> d; if (!loadCurtainFile(path, d, err)) { msg(err, MB_ICONERROR); return; }
        d->path = path; d->title = baseName(path); addDoc(std::move(d)); return;
    }
    auto mesh = std::make_shared<Mesh>();
    if (!loadMeshFile(path, *mesh, err)) { msg(err, MB_ICONERROR); return; }
    auto d = std::make_unique<ModelDoc>(); d->title = baseName(path); d->path.clear(); d->setMesh(mesh); d->dirty = false;
    addDoc(std::move(d));
}

static bool fileDialog(bool save, const wchar_t* filter, const wchar_t* defExt, std::string& path, const std::string& initial = "") {
    wchar_t buf[MAX_PATH * 2] = {}; if (!initial.empty()) wcsncpy(buf, W(initial).c_str(), MAX_PATH * 2 - 1);
    OPENFILENAMEW o = {sizeof o}; o.hwndOwner = g_main; o.lpstrFilter = filter; o.lpstrFile = buf; o.nMaxFile = MAX_PATH * 2; o.lpstrDefExt = defExt;
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return false;
    path = sys::narrow(buf); return true;
}
static void cmdOpen() {
    std::string p; if (fileDialog(false, L"3D models and images\0*.obj;*.stl;*.ply;*.gltf;*.glb;*.step;*.stp;*.curtain;*.png;*.jpg;*.jpeg;*.bmp\0Curtain projects\0*.curtain\0All files\0*.*\0", nullptr, p)) openPath(p);
}
static void cmdSaveAs() {
    ModelDoc* d = activeModel(); if (!d) return; std::string p;
    if (!fileDialog(true, L"Curtain project\0*.curtain\0", L"curtain", p, withExt(d->title, "curtain"))) return;
    if (saveCurtainFile(d, p)) { d->path = p; d->title = baseName(p); d->dirty = false; refreshTabs(); }
}
static void cmdSave() { ModelDoc* d = activeModel(); if (!d) return; if (d->path.empty() || extOf(d->path) != "curtain") { cmdSaveAs(); return; } if (saveCurtainFile(d, d->path)) { d->dirty = false; refreshTabs(); } }
static void cmdClose() {
    if (g_active < 0) return; Doc* d = g_docs[g_active].get();
    if (d->dirty && MessageBoxW(g_main, W("Close " + d->title + " without saving?").c_str(), L"Doppler's 3D Curtain", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g_docs.erase(g_docs.begin() + g_active); if (g_active >= (int)g_docs.size()) g_active = (int)g_docs.size() - 1;
    refreshTabs(); if (g_active >= 0) TabCtrl_SetCurSel(g_tab, g_active); showActive();
}

// ---- the .curtain project file (project.cpp)
static bool saveCurtainFile(ModelDoc* d, const std::string& path) {
    ensureCurtainNow(d); std::string err;
    if (!saveProject(*d, (d->curtain && !curtainStale(d)) ? d->curtain.get() : nullptr, path, err)) { msg(err, MB_ICONERROR); return false; }
    return true;
}
static bool loadCurtainFile(const std::string& path, std::unique_ptr<ModelDoc>& out, std::string& err) {
    auto d = std::make_unique<ModelDoc>(); if (!loadProject(path, *d, err)) return false; out = std::move(d); return true;
}

// ---- exports
static std::string identOf(const std::string& title) { std::string s = "curtain_"; for (char c : withExt(title, "")) s.push_back(isalnum((unsigned char)c) ? (char)tolower((unsigned char)c) : '_'); while (!s.empty() && s.back() == '_') s.pop_back(); return s; }
static void cmdExport(int id) {
    ModelDoc* d = activeModel(); if (!d || !d->cur.mesh) return; std::string p, err; const std::string base = withExt(d->title, "");
    if (id == IDM_EXP_OBJ || id == IDM_EXP_GLB) {
        const bool obj = id == IDM_EXP_OBJ;
        if (!fileDialog(true, obj ? L"Wavefront OBJ\0*.obj\0" : L"glTF binary\0*.glb\0", obj ? L"obj" : L"glb", p, base + (obj ? ".obj" : ".glb"))) return;
        if (!(obj ? saveObj(p, d->world, d->cur.mesh->idx, err) : saveGlb(p, d->world, d->cur.mesh->idx, err))) msg(err, MB_ICONERROR); return;
    }
    ensureCurtainNow(d); if (!d->curtain) { msg("The curtain could not be computed."); return; }
    if (id == IDM_EXP_HEADER) { if (!fileDialog(true, L"C++ header\0*.h\0", L"h", p, base + "_curtain.h")) return; if (!sys::writeFile(p, curtainHeaderText(d->curtain->field, identOf(d->title)))) msg("Cannot write " + p, MB_ICONERROR); }
    else if (id == IDM_EXP_BLOB) { if (!fileDialog(true, L"Curtain field\0*.cvf\0", L"cvf", p, base + ".cvf")) return; if (!sys::writeFile(p, curtainBlob(d->curtain->field))) msg("Cannot write " + p, MB_ICONERROR); }
    else if (id == IDM_EXP_CURTAIN_OBJ) { if (!fileDialog(true, L"Wavefront OBJ\0*.obj\0", L"obj", p, base + "_curtain.obj")) return; if (!saveObj(p, d->curtain->v, d->curtain->idx, err)) msg(err, MB_ICONERROR); }
}

// ---------------------------------------------------------------------------------------------------------------- image tab and queue
static void imgReadParams(ImageDoc* im) {
    wchar_t b[64]; GetWindowTextW(g_imgEdit[0], b, 64); im->seed = _wtoi(b); GetWindowTextW(g_imgEdit[1], b, 64); im->steps = std::max(1, std::min(200, _wtoi(b)));
    GetWindowTextW(g_imgEdit[2], b, 64); im->cfg = (float)_wtof(b); GetWindowTextW(g_imgEdit[3], b, 64); im->resolution = std::max(64, std::min(1024, _wtoi(b)));
    GetWindowTextW(g_imgEdit[4], b, 64); im->threshold = (float)_wtof(b); im->randomSeed = SendMessageW(g_imgRandom, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
static void refreshQueue() {
    ListView_DeleteAllItems(g_queue); std::lock_guard<std::mutex> g(g_jobMx);
    for (size_t i = 0; i < g_jobs.size(); i++) {
        std::wstring n = W(g_jobs[i].name), s = W(g_jobs[i].state); LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = (int)i; it.pszText = &n[0]; ListView_InsertItem(g_queue, &it);
        ListView_SetItemText(g_queue, (int)i, 1, &s[0]); std::wstring pc = std::to_wstring(g_jobs[i].percent) + L" %"; ListView_SetItemText(g_queue, (int)i, 2, &pc[0]);
    }
    ShowWindow(g_queue, g_jobs.empty() ? SW_HIDE : SW_SHOW); layout();
}
// ---- the shared queue of Image-to-3D jobs: one worker runs them one after the other
struct JobReq { uint64_t id; std::string image, title; H3DParams p; std::shared_ptr<std::atomic<bool>> cancel; };
struct JobUpdate { uint64_t id; std::string state; int percent; };
struct JobDone { uint64_t id; std::string title; bool ok; std::string err; std::shared_ptr<Mesh> mesh; };
static std::deque<JobReq> g_pendingJobs; static std::map<uint64_t, std::shared_ptr<std::atomic<bool>>> g_cancels; static bool g_workerBusy = false;
static void jobWorker() {
    for (;;) {
        JobReq j;
        { std::lock_guard<std::mutex> g(g_jobMx); if (g_pendingJobs.empty()) { g_workerBusy = false; return; } j = g_pendingJobs.front(); g_pendingJobs.pop_front(); }
        if (j.cancel->load()) { PostMessageW(g_main, WM_JOB_DONE, 0, (LPARAM) new JobDone{j.id, j.title, false, "cancelled", nullptr}); continue; }
        auto mesh = std::make_shared<Mesh>(); std::string err;
        auto prog = [&](const std::string& s, float f) { PostMessageW(g_main, WM_JOB_UPDATE, 0, (LPARAM) new JobUpdate{j.id, s, (int)(f * 100)}); return !j.cancel->load(); };
        const bool ok = imageToMesh(j.image, j.p, *mesh, prog, err);
        PostMessageW(g_main, WM_JOB_DONE, 0, (LPARAM) new JobDone{j.id, j.title, ok, err, mesh});
    }
}
static void cmdConvert() {
    ImageDoc* im = activeImage(); if (!im) return; imgReadParams(im);
    if (!hunyuanInstalled()) {
        if (MessageBoxW(g_main, L"The Hunyuan3D model is not downloaded yet (4.9 GB).\n\nOpen the download window?", L"Doppler's 3D Curtain", MB_YESNO | MB_ICONQUESTION) == IDYES) showSetupDialog(g_main, false);
        if (!hunyuanInstalled()) return;
    }
    if (!cudaInstalled() && MessageBoxW(g_main, L"The CUDA libraries are not installed, so the network would run on the processor: about 20 minutes or more instead of about a minute.\n\nContinue on the processor?", L"Doppler's 3D Curtain", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (im->randomSeed) im->seed = (int)(GetTickCount() & 0x7FFFFFFF);
    JobReq r; r.title = im->title; r.image = im->path; r.cancel = std::make_shared<std::atomic<bool>>(false);
    r.p.seed = (unsigned)im->seed; r.p.steps = im->steps; r.p.cfg = im->cfg; r.p.resolution = im->resolution; r.p.threshold = im->threshold; r.p.checkpoint = hunyuanModelPath();
    Job j; { std::lock_guard<std::mutex> g(g_jobMx); r.id = j.id = g_nextJob++; j.name = "Image to 3D: " + im->title + "  (seed " + std::to_string(im->seed) + ")"; j.state = "Waiting"; g_jobs.push_back(j); g_cancels[r.id] = r.cancel; g_pendingJobs.push_back(r); if (!g_workerBusy) { g_workerBusy = true; std::thread(jobWorker).detach(); } }
    refreshQueue(); updateStatus();
}
static void jobUpdate(JobUpdate* u) {
    { std::lock_guard<std::mutex> g(g_jobMx); for (auto& j : g_jobs) if (j.id == u->id) { j.state = u->state; j.percent = u->percent; } }
    delete u; refreshQueue();
}
static void jobDone(JobDone* d) {
    { std::lock_guard<std::mutex> g(g_jobMx); for (auto& j : g_jobs) if (j.id == d->id) { j.state = d->ok ? "Done: opened in a new tab" : (d->err == "cancelled" ? "Cancelled" : "Failed: " + d->err); j.percent = d->ok ? 100 : j.percent; } g_cancels.erase(d->id); }
    if (d->ok && d->mesh) {
        auto doc = std::make_unique<ModelDoc>(); doc->title = d->title + " (3D)"; doc->setMesh(d->mesh); doc->dirty = true; addDoc(std::move(doc));
    }
    delete d; refreshQueue();
}
static void jobCancelRow(int row) {
    uint64_t id = 0; { std::lock_guard<std::mutex> g(g_jobMx); if (row >= 0 && row < (int)g_jobs.size()) id = g_jobs[row].id; }
    if (!id) return; std::shared_ptr<std::atomic<bool>> c; { std::lock_guard<std::mutex> g(g_jobMx); auto it = g_cancels.find(id); if (it != g_cancels.end()) c = it->second; }
    if (c && MessageBoxW(g_main, L"Cancel this job?", L"Doppler's 3D Curtain", MB_YESNO | MB_ICONQUESTION) == IDYES) { *c = true; std::lock_guard<std::mutex> g(g_jobMx); for (auto& j : g_jobs) if (j.id == id) j.state = "Cancelling..."; }
    refreshQueue();
}static LRESULT CALLBACK imgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); FillRect(dc, &rc, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
        if (ImageDoc* im = activeImage()) {
            const int panelW = 300, aw = rc.right - panelW - 20, ah = rc.bottom - 20;
            if (im->bmp && aw > 10 && ah > 10) {
                double s = std::min((double)aw / im->w, (double)ah / im->h); int dw = (int)(im->w * s), dh = (int)(im->h * s), x = 10 + (aw - dw) / 2, y = 10 + (ah - dh) / 2;
                HDC mdc = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mdc, im->bmp); SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
                StretchBlt(dc, x, y, dw, dh, mdc, 0, 0, im->w, im->h, SRCCOPY); SelectObject(mdc, old); DeleteDC(mdc);
            }
        }
        EndPaint(h, &ps); return 0;
    }
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_COMMAND) { SendMessageW(g_main, WM_COMMAND, wp, lp); return 0; }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------------------------------------------------------------------------------------------------------------- layout and creation
static void layout() {
    if (!g_main) return;
    RECT rc; GetClientRect(g_main, &rc); const int Wd = rc.right, Hd = rc.bottom;
    const int tbH = toolbarHeight(); MoveWindow(g_toolbar, 0, 0, Wd, tbH, TRUE);
    SendMessageW(g_status, WM_SIZE, 0, 0); RECT sr; GetWindowRect(g_status, &sr); const int stH = sr.bottom - sr.top;
    const int tabH = 28, qH = IsWindowVisible(g_queue) ? 110 : 0, top = tbH + tabH, ch = std::max(50, Hd - top - stH - qH);
    MoveWindow(g_tab, 0, tbH, Wd, tabH, TRUE);
    MoveWindow(g_view, 0, top, Wd, ch, TRUE); MoveWindow(g_imgPanel, 0, top, Wd, ch, TRUE);
    if (qH) MoveWindow(g_queue, 0, Hd - stH - qH, Wd, qH, TRUE);
    MoveWindow(g_welcome, Wd / 2 - 320, top + ch / 2 - 30, 640, 60, TRUE);
    // overlay buttons
    int y = top + 12;
    for (int i = 0; i < 7; i++) { SetWindowPos(g_viewBtn[i], HWND_TOP, 12, y, 84, 26, SWP_NOACTIVATE); y += (i == 0 ? 34 : 30); }
    y += 6; SetWindowPos(g_preview, HWND_TOP, 12, y, 84, 28, SWP_NOACTIVATE); SetWindowPos(g_hide, HWND_TOP, 12, y + 32, 84, 28, SWP_NOACTIVATE); SetWindowPos(g_alpha, HWND_TOP, 36, y + 66, 36, 120, SWP_NOACTIVATE);
    // image tab controls on the right
    const int px = Wd - 290; int iy = 24;
    for (int i = 0; i < 5; i++) { SetWindowPos(g_imgLabel[i], nullptr, px, top + iy + 3, 130, 20, SWP_NOZORDER); SetWindowPos(g_imgEdit[i], nullptr, px + 140, top + iy, 120, 24, SWP_NOZORDER); iy += 34; if (i == 0) { SetWindowPos(g_imgRandom, nullptr, px + 140, top + iy, 130, 20, SWP_NOZORDER); iy += 26; } }
    SetWindowPos(g_imgConvert, nullptr, px, top + iy + 10, 260, 36, SWP_NOZORDER); SetWindowPos(g_imgInfo, nullptr, px, top + iy + 56, 260, 120, SWP_NOZORDER);
    if (g_tool != Tool::None && activeModel()) panelConfigure();
}

static HMENU buildMenu() {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), exp = CreatePopupMenu(), edit = CreatePopupMenu(), model = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O"); AppendMenuW(file, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S"); AppendMenuW(file, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(exp, MF_STRING, IDM_EXP_HEADER, L"Curtain for the engine - C++ header (.h)..."); AppendMenuW(exp, MF_STRING, IDM_EXP_BLOB, L"Curtain for the engine - binary field (.cvf)...");
    AppendMenuW(exp, MF_STRING, IDM_EXP_CURTAIN_OBJ, L"Curtain surface - OBJ..."); AppendMenuW(exp, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(exp, MF_STRING, IDM_EXP_OBJ, L"Object - OBJ..."); AppendMenuW(exp, MF_STRING, IDM_EXP_GLB, L"Object - GLB...");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)exp, L"&Export"); AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_DOWNLOAD, L"&Download models and CUDA libraries..."); AppendMenuW(file, MF_STRING, IDM_CLOSE, L"&Close tab\tCtrl+W"); AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z"); AppendMenuW(edit, MF_STRING, IDM_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(model, MF_STRING, IDM_CENTER, L"&Center on the plate"); AppendMenuW(model, MF_STRING, IDM_RESET, L"&Reset size, rotation and position"); AppendMenuW(model, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(model, MF_STRING, IDM_MODE_SMOOTH, L"Curtain: &Smooth - sphere limit"); AppendMenuW(model, MF_STRING, IDM_MODE_HULL, L"Curtain: Convex &hull - distance"); AppendMenuW(model, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(model, MF_STRING, IDM_PREVIEW, L"&Preview curtain\tP"); AppendMenuW(model, MF_STRING, IDM_HIDE, L"&Hide object (wrap only)\tH"); AppendMenuW(model, MF_STRING, IDM_VERTICES, L"Show &vertices");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File"); AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit"); AppendMenuW(bar, MF_POPUP, (UINT_PTR)model, L"&Model"); AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_COMMAND || m == WM_HSCROLL) return SendMessageW(g_main, m, wp, lp);
    if (m == WM_CTLCOLORSTATIC) { SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE); }
    if (m == WM_ERASEBKGND) { RECT rc; GetClientRect(h, &rc); FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_BTNFACE)); DrawEdge((HDC)wp, &rc, EDGE_RAISED, BF_RECT); return 1; }
    return DefWindowProcW(h, m, wp, lp);
}
static WNDPROC g_editOrig;
static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN && wp == VK_RETURN) { SetFocus(g_main); return 0; }
    if (m == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return CallWindowProcW(g_editOrig, h, m, wp, lp);
}

static void createChildren() {
    NONCLIENTMETRICSW ncm = {sizeof ncm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont); ncm.lfMessageFont.lfWeight = FW_BOLD; g_bold = CreateFontIndirectW(&ncm.lfMessageFont);
    auto mk = [&](HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) { HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_inst, nullptr); SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c; };
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_WIN95_CLASSES}; InitCommonControlsEx(&ic);
    g_toolbar = createToolbar(g_main, g_inst);
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, 0, 0, 10, 10, g_main, (HMENU)IDC_TAB, g_inst, nullptr); SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, TRUE);
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g_main, (HMENU)IDC_STATUS, g_inst, nullptr); SendMessageW(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);
    g_view = createView(g_main, g_inst);
    // image panel
    WNDCLASSW ic2 = {}; ic2.lpfnWndProc = imgProc; ic2.hInstance = g_inst; ic2.hCursor = LoadCursorW(nullptr, IDC_ARROW); ic2.lpszClassName = L"D3CImage"; RegisterClassW(&ic2);
    g_imgPanel = CreateWindowExW(0, L"D3CImage", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 10, 10, g_main, nullptr, g_inst, nullptr);
    const wchar_t* il[5] = {L"Seed", L"Steps", L"CFG (guidance)", L"Voxel resolution", L"Surface threshold"}; const wchar_t* iv[5] = {L"0", L"20", L"8.0", L"256", L"0.70"};
    for (int i = 0; i < 5; i++) { g_imgLabel[i] = mk(g_main, L"STATIC", il[i], SS_LEFT, 0); g_imgEdit[i] = mk(g_main, L"EDIT", iv[i], ES_AUTOHSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE); ShowWindow(g_imgLabel[i], SW_HIDE); ShowWindow(g_imgEdit[i], SW_HIDE); }
    g_imgRandom = mk(g_main, L"BUTTON", L"Random seed", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_RANDOM); g_imgConvert = mk(g_main, L"BUTTON", L"Convert to 3D", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_CONVERT);
    g_imgInfo = mk(g_main, L"STATIC", L"Image to 3D uses the Hunyuan3D 2.0 shape model. Jobs go to the queue below; the finished model opens in a new tab.", SS_LEFT, 0);
    for (HWND h : {g_imgRandom, g_imgConvert, g_imgInfo}) ShowWindow(h, SW_HIDE);
    // queue
    g_queue = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER, 0, 0, 10, 10, g_main, (HMENU)IDC_QUEUE, g_inst, nullptr); SendMessageW(g_queue, WM_SETFONT, (WPARAM)g_font, TRUE);
    ListView_SetExtendedListViewStyle(g_queue, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    { const wchar_t* c[] = {L"Job", L"State", L"Progress"}; const int w[] = {320, 560, 80}; for (int i = 0; i < 3; i++) { LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (LPWSTR)c[i]; col.cx = w[i]; ListView_InsertColumn(g_queue, i, &col); } }
    g_welcome = mk(g_main, L"STATIC", L"Open a 3D model (OBJ, STL, PLY, glTF/GLB) or a picture to turn into a model.\r\nFile > Open, or drop a file on this window.", SS_CENTER, 0);
    // view overlay: quick view stack and the curtain preview switch
    const wchar_t* vn[7] = {L"Center", L"Left", L"Right", L"Front", L"Back", L"Top", L"Bottom"};
    for (int i = 0; i < 7; i++) g_viewBtn[i] = mk(g_main, L"BUTTON", vn[i], BS_PUSHBUTTON | WS_CLIPSIBLINGS, IDC_VIEWBASE + i);
    g_preview = mk(g_main, L"BUTTON", L"Curtain", BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_CLIPSIBLINGS, IDC_PREVIEW);
    g_hide = mk(g_main, L"BUTTON", L"Hide object", BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_CLIPSIBLINGS, IDC_HIDE);
    g_alpha = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_VERT | TBS_NOTICKS | WS_CLIPSIBLINGS, 0, 0, 10, 10, g_main, (HMENU)IDC_ALPHA, g_inst, nullptr);
    SendMessageW(g_alpha, TBM_SETRANGE, TRUE, MAKELONG(0, 95));
    // tool panel
    WNDCLASSW pc = {}; pc.lpfnWndProc = panelProc; pc.hInstance = g_inst; pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); pc.lpszClassName = L"D3CPanel"; RegisterClassW(&pc);
    g_panel = CreateWindowExW(WS_EX_CONTROLPARENT, L"D3CPanel", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 640, 140, g_main, nullptr, g_inst, nullptr);
    P.title = mk(g_panel, L"STATIC", L"", SS_LEFT, 0); SendMessageW(P.title, WM_SETFONT, (WPARAM)g_bold, TRUE);
    for (int r = 0; r < 3; r++) {
        P.label[r] = mk(g_panel, L"STATIC", L"", SS_LEFT, IDC_ROWBASE + r * 3); P.tb[r] = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP, 0, 0, 10, 10, g_panel, (HMENU)(INT_PTR)(IDC_ROWBASE + r * 3 + 1), g_inst, nullptr);
        P.ed[r] = mk(g_panel, L"EDIT", L"", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP, IDC_ROWBASE + r * 3 + 2, WS_EX_CLIENTEDGE);
        g_editOrig = (WNDPROC)SetWindowLongPtrW(P.ed[r], GWLP_WNDPROC, (LONG_PTR)editProc);
    }
    P.apply = mk(g_panel, L"BUTTON", L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_APPLY); P.uniform = mk(g_panel, L"BUTTON", L"Uniform", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_UNIFORM);
    SendMessageW(P.uniform, BM_SETCHECK, BST_CHECKED, 0); setUniformScale(true);
    P.mode = mk(g_panel, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_MODE); for (auto t : kModeText) SendMessageW(P.mode, CB_ADDSTRING, 0, (LPARAM)t);
    ShowWindow(g_panel, SW_HIDE);
    for (HWND h : g_viewBtn) ShowWindow(h, SW_HIDE); ShowWindow(g_preview, SW_HIDE); ShowWindow(g_hide, SW_HIDE);
}

static void trackToRow(int row, bool commit) {
    if (g_syncing || row >= P.rows) return;
    double v = valueFromTb(row, (int)SendMessageW(P.tb[row], TBM_GETPOS, 0, 0));
    if (g_tool == Tool::Details) { g_syncing = true; fmtEdit(P.ed[0], v, P.dec[0]); g_syncing = false; return; }
    rowChanged(row, v, commit);
}

static LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {960, 640}; return 0;
    case WM_DROPFILES: { HDROP dr = (HDROP)wp; UINT n = DragQueryFileW(dr, 0xFFFFFFFF, nullptr, 0); for (UINT i = 0; i < n; i++) { wchar_t b[MAX_PATH * 2]; DragQueryFileW(dr, i, b, MAX_PATH * 2); openPath(sys::narrow(b)); } DragFinish(dr); return 0; }
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)lp;
        if (n->hwndFrom == g_tab && n->code == TCN_SELCHANGE) { g_active = TabCtrl_GetCurSel(g_tab); showActive(); }
        else if (n->hwndFrom == g_queue && n->code == NM_DBLCLK) jobCancelRow(((NMITEMACTIVATE*)lp)->iItem);
        return 0;
    }
    case WM_TIMER: if (wp == TIMER_CURTAIN) { KillTimer(h, TIMER_CURTAIN); if (ModelDoc* d = activeModel()) requestCurtain(d); } return 0;
    case WM_JOB_UPDATE: jobUpdate((JobUpdate*)lp); return 0;
    case WM_JOB_DONE: jobDone((JobDone*)lp); return 0;
    case WM_CURTAIN_DONE: {
        CurJob* j = (CurJob*)lp;
        for (auto& dd : g_docs) if (dd->id == j->docId && dd->kind == Doc::Model) {
            ModelDoc* d = static_cast<ModelDoc*>(dd.get());
            if (d->geomVer == j->geomVer) { d->curtain = j->res; d->curtainGeomVer = j->geomVer; d->curtainParams = j->p; }
            d->curtainBusy = false;
        }
        delete j; updateStatus(); viewRedraw(); return 0;
    }
    case WM_HSCROLL: case WM_VSCROLL: {
        HWND c = (HWND)lp; const WORD code = LOWORD(wp);
        if (c == g_alpha) { if (ModelDoc* d = activeModel()) { d->alpha = 1.0f - (float)SendMessageW(g_alpha, TBM_GETPOS, 0, 0) / 100.0f; viewRedraw(); } return 0; }
        for (int r = 0; r < 3; r++) if (c == P.tb[r]) trackToRow(r, code != TB_THUMBTRACK);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= IDC_VIEWBASE && id < IDC_VIEWBASE + 7) { static const char* names[] = {"center", "left", "right", "front", "back", "top", "bottom"}; viewSetPreset(names[id - IDC_VIEWBASE]); return 0; }
        if (id >= IDC_ROWBASE && id < IDC_ROWBASE + 9 && (id - IDC_ROWBASE) % 3 == 2 && code == EN_KILLFOCUS && !g_syncing) {
            int r = (id - IDC_ROWBASE) / 3; wchar_t b[64]; GetWindowTextW(P.ed[r], b, 64); double v = _wtof(b);
            if (g_tool == Tool::Details) { v = std::min(100.0, std::max(0.0, v)); g_syncing = true; SendMessageW(P.tb[0], TBM_SETPOS, TRUE, (LPARAM)std::lround(v * 100)); fmtEdit(P.ed[0], v, 2); g_syncing = false; }
            else rowChanged(r, v, true);
            return 0;
        }
        switch (id) {
        case IDM_OPEN: cmdOpen(); break; case IDM_SAVE: cmdSave(); break; case IDM_SAVEAS: cmdSaveAs(); break; case IDM_CLOSE: cmdClose(); break; case IDM_EXIT: PostMessageW(h, WM_CLOSE, 0, 0); break;
        case IDM_EXP_HEADER: case IDM_EXP_BLOB: case IDM_EXP_CURTAIN_OBJ: case IDM_EXP_OBJ: case IDM_EXP_GLB: cmdExport(id); break;
        case IDM_DOWNLOAD: showSetupDialog(h, false); break;
        case IDM_UNDO: if (ModelDoc* d = activeModel()) if (d->doUndo()) { refreshCommands(); refreshTabs(); panelSync(); updateStatus(); if (curtainWanted(d)) requestCurtain(d); viewRedraw(); } break;
        case IDM_REDO: if (ModelDoc* d = activeModel()) if (d->doRedo()) { refreshCommands(); refreshTabs(); panelSync(); updateStatus(); if (curtainWanted(d)) requestCurtain(d); viewRedraw(); } break;
        case IDM_CENTER: if (ModelDoc* d = activeModel()) { d->beginEdit(); d->centerOnPlate(); d->endEdit(); onEditChanged(d, true); viewRedraw(); } break;
        case IDM_RESET: if (ModelDoc* d = activeModel()) { d->beginEdit(); d->cur.xf = Xform(); d->cur.xf.pos = d->pivot; d->centerOnPlate(); d->endEdit(); onEditChanged(d, true); viewRedraw(); } break;
        case IDM_MODE_SMOOTH: case IDM_MODE_HULL: if (ModelDoc* d = activeModel()) { d->beginEdit(); d->cur.cp.mode = id == IDM_MODE_HULL ? CurtainMode::Hull : CurtainMode::Smooth; d->endEdit(); refreshCommands(); refreshTabs(); panelSync(); if (curtainWanted(d)) requestCurtain(d); viewRedraw(); } break;
        case IDM_VERTICES: if (ModelDoc* d = activeModel()) { d->showVertices = !d->showVertices; refreshCommands(); viewRedraw(); } break;
        case IDM_PREVIEW: case IDC_PREVIEW:
            if (g_syncing) break;
            if (ModelDoc* d = activeModel()) { d->showCurtain = (id == IDC_PREVIEW) ? SendMessageW(g_preview, BM_GETCHECK, 0, 0) == BST_CHECKED : !d->showCurtain; ShowWindow(g_alpha, d->showCurtain ? SW_SHOW : SW_HIDE); refreshCommands(); if (curtainWanted(d)) requestCurtain(d); viewRedraw(); }
            break;
        case IDM_HIDE: case IDC_HIDE:
            if (g_syncing) break;
            if (ModelDoc* d = activeModel()) {
                d->hideObject = (id == IDC_HIDE) ? SendMessageW(g_hide, BM_GETCHECK, 0, 0) == BST_CHECKED : !d->hideObject;
                if (d->hideObject && !d->showCurtain) { d->showCurtain = true; ShowWindow(g_alpha, SW_SHOW); }               // wrap only: the wrap must be visible
                refreshCommands(); if (curtainWanted(d)) requestCurtain(d); viewRedraw();
            }
            break;
        case IDT_DISTANCE: setTool(Tool::Distance); break; case IDT_PRECISION: setTool(Tool::Precision); break; case IDT_SCALE: setTool(Tool::Scale); break;
        case IDT_DENSITY: setTool(Tool::Density); break;
        case IDT_ERASER: setTool(Tool::Eraser); break;
        case IDT_ROTATE: setTool(Tool::Rotate); break; case IDT_MOVE: setTool(Tool::Move); break; case IDT_DETAILS: setTool(Tool::Details); break;
        case IDC_APPLY: { wchar_t b[64]; GetWindowTextW(P.ed[0], b, 64); applyDetails(std::min(100.0, std::max(0.0, _wtof(b)))); break; }
        case IDC_UNIFORM: setUniformScale(SendMessageW(P.uniform, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
        case IDC_MODE: if (code == CBN_SELCHANGE) if (ModelDoc* d = activeModel()) { int s = (int)SendMessageW(P.mode, CB_GETCURSEL, 0, 0); d->beginEdit(); d->cur.cp.mode = s == 1 ? CurtainMode::Hull : CurtainMode::Smooth; d->endEdit(); refreshCommands(); refreshTabs(); requestCurtain(d); viewRedraw(); } break;
        case IDC_CONVERT: cmdConvert(); break;
        case IDM_ABOUT: msg("Doppler's 3D Curtain\n\nWraps a mathematical curtain (a distance field) around 3D models for the procedural engine, and turns pictures into 3D models.\n\nFree for personal use; enterprise use requires US$100 per workstation (see LICENSE)."); break;
        }
        return 0;
    }
    case WM_APP + 99: {                                                              // test aid: snapshot of the 3D view into %D3C_SNAPDIR%, one file per call
        static int n = 0; const char* dir = getenv("D3C_SNAPDIR"); if (dir) { char b[600]; snprintf(b, sizeof b, "%s\snap_%02d.bmp", dir, ++n); viewSnapshot(b); }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst; SetProcessDPIAware(); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {sizeof wc}; wc.lpfnWndProc = mainProc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"D3CMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon; RegisterClassExW(&wc);
    g_main = CreateWindowExW(WS_EX_ACCEPTFILES, L"D3CMain", L"Doppler's 3D Curtain", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 940, nullptr, buildMenu(), inst, nullptr);
    createChildren(); layout(); refreshCommands(); updateStatus(); showActive();
    ShowWindow(g_main, show); UpdateWindow(g_main);
    // first start: offer the downloads once
    { const std::wstring ini = W(sys::join(sys::exeDir(), "settings.ini"));
      if (GetPrivateProfileIntW(L"app", L"setupShown", 0, ini.c_str()) == 0 && (!hunyuanInstalled() || !cudaInstalled())) { WritePrivateProfileStringW(L"app", L"setupShown", L"1", ini.c_str()); showSetupDialog(g_main, true); } }
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); for (int i = 1; i < argc; i++) openPath(sys::narrow(argv[i])); LocalFree(argv);
    HACCEL acc; { ACCEL a[] = {{FCONTROL | FVIRTKEY, 'O', IDM_OPEN}, {FCONTROL | FVIRTKEY, 'S', IDM_SAVE}, {FCONTROL | FVIRTKEY, 'Z', IDM_UNDO}, {FCONTROL | FVIRTKEY, 'Y', IDM_REDO}, {FCONTROL | FVIRTKEY, 'W', IDM_CLOSE}}; acc = CreateAcceleratorTableW(a, 5); }
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'P' || msg.wParam == 'H') && GetFocus() != P.ed[0] && GetFocus() != P.ed[1] && GetFocus() != P.ed[2] && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && (GetFocus() == g_view || GetFocus() == g_main || GetFocus() == g_tab)) { PostMessageW(g_main, WM_COMMAND, msg.wParam == 'H' ? IDM_HIDE : IDM_PREVIEW, 0); continue; }
        if (!TranslateAcceleratorW(g_main, acc, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
