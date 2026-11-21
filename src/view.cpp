// The 3D view: OpenGL 1.x (no extension loader needed), Cura-like camera and tool handles.
//   right button drag = rotate/tilt, middle button or Shift + left drag = pan, wheel = zoom, left drag on the object = move it on the plate.
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstdio>
#include "app.h"

namespace {
HWND g_view = nullptr; HDC g_dc = nullptr; HGLRC g_rc = nullptr;
const float kFov = 45.0f;

struct Cam { Vec3 eye, fwd, right, up; };
Cam camOf(const Camera& c) {
    Cam r; float cy = std::cos(rad(c.yaw)), sy = std::sin(rad(c.yaw)), cp = std::cos(rad(c.pitch)), sp = std::sin(rad(c.pitch));
    r.eye = c.target + Vec3(cp * cy, cp * sy, sp) * c.dist;
    r.fwd = normalized(c.target - r.eye); r.right = normalized(cross(r.fwd, Vec3(0, 0, 1))); r.up = cross(r.right, r.fwd);
    return r;
}
void ray(const Camera& c, int px, int py, int W, int H, Vec3& o, Vec3& d) {
    Cam k = camOf(c); const float th = std::tan(rad(kFov) * 0.5f), asp = (float)W / std::max(1, H);
    float nx = 2.0f * px / std::max(1, W) - 1.0f, ny = 1.0f - 2.0f * py / std::max(1, H);
    o = k.eye; d = normalized(k.fwd + k.right * (nx * th * asp) + k.up * (ny * th));
}

// ------------------------------------------------------------------ gizmo geometry
enum { H_NONE = 0, H_X = 1, H_Y = 2, H_Z = 3, H_ALL = 4 };
int g_hover = H_NONE;
float gizmoLen(ModelDoc* d) { return d->cam.dist * 0.2f; }
Vec3 axisDir(ModelDoc* d, int i, bool local) {                         // world axis i, or the object's own axis i
    Vec3 e(0, 0, 0); e[i] = 1;
    if (!local) return e;
    Mat4 R = Mat4::rotZ(rad(d->cur.xf.rotDeg.z)) * Mat4::rotY(rad(d->cur.xf.rotDeg.y)) * Mat4::rotX(rad(d->cur.xf.rotDeg.x));
    return normalized(R.dir(e));
}
// parameter along the axis line (c + t a) at the point closest to the ray
bool axisParam(const Vec3& o, const Vec3& dir, const Vec3& c, const Vec3& a, float& t, float& distOut) {
    Vec3 w0 = o - c; float b = dot(dir, a), dd = dot(dir, w0), e = dot(a, w0), den = 1 - b * b;
    if (std::fabs(den) < 1e-5f) return false;
    float s = (b * e - dd) / den; t = e + s * b;
    distOut = length((o + dir * s) - (c + a * t));
    return true;
}
bool planeHit(const Vec3& o, const Vec3& dir, const Vec3& p, const Vec3& n, Vec3& hit) {
    float den = dot(dir, n); if (std::fabs(den) < 1e-6f) return false;
    float s = dot(p - o, n) / den; if (s < 0) return false; hit = o + dir * s; return true;
}
bool rayMesh(ModelDoc* d, const Vec3& o, const Vec3& dir, float& tBest) {
    const auto& v = d->world; const auto& idx = d->cur.mesh->idx; bool hit = false; tBest = 1e30f;
    // bounding box first
    float t0 = 0, t1 = 1e30f;
    for (int a = 0; a < 3; a++) {
        float inv = std::fabs(dir[a]) < 1e-12f ? 1e12f : 1.0f / dir[a]; float n0 = (d->wlo[a] - o[a]) * inv, n1 = (d->whi[a] - o[a]) * inv; if (n0 > n1) std::swap(n0, n1);
        t0 = std::max(t0, n0); t1 = std::min(t1, n1); if (t0 > t1) return false;
    }
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vec3 &a = v[idx[i]], &b = v[idx[i + 1]], &c = v[idx[i + 2]];
        Vec3 e1 = b - a, e2 = c - a, p = cross(dir, e2); float det = dot(e1, p); if (std::fabs(det) < 1e-12f) continue;
        float inv = 1 / det; Vec3 s = o - a; float u = dot(s, p) * inv; if (u < 0 || u > 1) continue;
        Vec3 q = cross(s, e1); float w = dot(dir, q) * inv; if (w < 0 || u + w > 1) continue;
        float t = dot(e2, q) * inv; if (t > 0 && t < tBest) { tBest = t; hit = true; }
    }
    return hit;
}
Mat4 eulerMat(const Vec3& deg) { return Mat4::rotZ(rad(deg.z)) * Mat4::rotY(rad(deg.y)) * Mat4::rotX(rad(deg.x)); }
Vec3 matEuler(const Mat4& R) {                                          // R = Rz Ry Rx
    auto M = [&](int r, int c) { return R.m[c * 4 + r]; };
    float sy = std::min(1.0f, std::max(-1.0f, -M(2, 0))), y = std::asin(sy), x, z;
    if (std::fabs(sy) < 0.99999f) { x = std::atan2(M(2, 1), M(2, 2)); z = std::atan2(M(1, 0), M(0, 0)); }
    else { x = std::atan2(-M(1, 2), M(1, 1)); z = 0; }
    return Vec3(x * 180 / kPi, y * 180 / kPi, z * 180 / kPi);
}

// ------------------------------------------------------------------ dragging
struct Drag {
    int mode = 0;                                   // 1 orbit, 2 pan, 3 move axis, 4 move on plate, 5 scale axis, 6 rotate, 7 uniform scale
    int axis = 0; POINT last{0, 0}; POINT start{0, 0};
    Xform xf0; float t0 = 0, planeZ = 0; Vec3 p0, c0, a0; float ang0 = 0; bool uniform = false;
} g_drag;
bool g_uniformScale = true;                        // set by the main window (the "Uniform" tick of the size panel)

int hitGizmo(ModelDoc* d, int px, int py, int W, int H, float* tOut = nullptr, Vec3* hitOut = nullptr) {
    Tool tool = activeTool(); if (tool != Tool::Move && tool != Tool::Scale && tool != Tool::Rotate) return H_NONE;
    Vec3 o, dir; ray(d->cam, px, py, W, H, o, dir); const Vec3 c = d->center(); const float L = gizmoLen(d), tol = L * 0.09f;
    int best = H_NONE; float bestT = 1e30f;
    if (tool == Tool::Move || tool == Tool::Scale) {
        for (int i = 0; i < (tool == Tool::Move ? 2 : 3); i++) {
            Vec3 a = axisDir(d, i, tool == Tool::Scale); float t, dist; if (!axisParam(o, dir, c, a, t, dist)) continue;
            if (t > L * 0.15f && t < L * 1.15f && dist < tol && t < bestT) { best = H_X + i; bestT = t; }
        }
        if (tool == Tool::Scale && best == H_NONE) { Vec3 q = c - o; float s = dot(q, dir); if (s > 0 && length(o + dir * s - c) < tol * 1.3f) best = H_ALL; }
    } else {
        for (int i = 0; i < 3; i++) {
            Vec3 n = axisDir(d, i, false), hit; if (!planeHit(o, dir, c, n, hit)) continue;
            float r = length(hit - c); if (std::fabs(r - L) < tol) { float s = dot(hit - o, dir); if (s < bestT) { bestT = s; best = H_X + i; if (hitOut) *hitOut = hit; } }
        }
    }
    if (tOut) *tOut = bestT; return best;
}

// ------------------------------------------------------------------ drawing
void drawGrid(ModelDoc* d) {
    const float diag = std::max(d->diag(), 1e-6f); float step = std::pow(10.0f, std::floor(std::log10(diag / 3.0f)));
    if (diag / step > 20) step *= 2; if (diag / step > 20) step *= 2.5f;
    const float E = std::ceil(std::max(diag * 2.5f, step * 10) / step) * step; const float z = -diag * 1e-4f;
    glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glColor4f(0.10f, 0.11f, 0.14f, 0.55f); glBegin(GL_QUADS); glVertex3f(-E, -E, z * 2); glVertex3f(E, -E, z * 2); glVertex3f(E, E, z * 2); glVertex3f(-E, E, z * 2); glEnd();
    glEnable(GL_LINE_SMOOTH); glLineWidth(1.0f); glBegin(GL_LINES);
    int n = (int)std::lround(E / step);
    for (int i = -n; i <= n; i++) {
        bool major = (i % 5) == 0; if (major) glColor4f(0.60f, 0.64f, 0.72f, 0.60f); else glColor4f(0.45f, 0.48f, 0.55f, 0.32f);
        glVertex3f(i * step, -E, z); glVertex3f(i * step, E, z); glVertex3f(-E, i * step, z); glVertex3f(E, i * step, z);
    }
    glColor4f(0.95f, 0.30f, 0.30f, 0.9f); glVertex3f(-E, 0, z); glVertex3f(E, 0, z);        // X axis
    glColor4f(0.35f, 0.90f, 0.40f, 0.9f); glVertex3f(0, -E, z); glVertex3f(0, E, z);        // Y axis
    glEnd(); glDepthMask(GL_TRUE);
}
void drawObject(ModelDoc* d) {
    if (!d->cur.mesh) return;
    glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3, GL_FLOAT, 0, d->world.data());
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_NORMALIZE); glEnable(GL_COLOR_MATERIAL); glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, d->wnormal.data());
    glColor3f(0.80f, 0.80f, 0.84f); glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f, 1.0f);
    glDrawElements(GL_TRIANGLES, (GLsizei)d->cur.mesh->idx.size(), GL_UNSIGNED_INT, d->cur.mesh->idx.data());
    glDisable(GL_POLYGON_OFFSET_FILL); glDisableClientState(GL_NORMAL_ARRAY); glDisable(GL_LIGHTING);
    if (d->showVertices && d->world.size() < 1500000) {                                                 // the vertices are blue
        glPointSize(d->world.size() > 60000 ? 1.0f : (d->world.size() > 15000 ? 2.0f : 3.0f)); glColor3f(0.15f, 0.42f, 1.0f); glDrawArrays(GL_POINTS, 0, (GLsizei)d->world.size());
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}
void drawCurtain(ModelDoc* d, bool editMode) {
    if (!d->curtain || d->curtainGeomVer != d->geomVer || d->curtain->idx.empty()) return;
    const CurtainResult& c = *d->curtain;
    const float a = editMode ? 0.14f : d->alpha;
    const float wireA = std::min(0.45f, 0.45f * std::sqrt(8000.0f / std::max(1.0f, (float)(c.idx.size() / 3))) + 0.03f);    // lines fade out when there are very many triangles
    float r = editMode ? 1.0f : 1.0f, g = editMode ? 0.15f : 0.85f, b = editMode ? 0.10f : 0.10f;            // red while editing, yellow in the view
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3, GL_FLOAT, 0, c.v.data()); glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, c.nrm.data());
    glEnable(GL_LIGHTING); glEnable(GL_COLOR_MATERIAL); glColor4f(r, g, b, a);
    glDrawElements(GL_TRIANGLES, (GLsizei)c.idx.size(), GL_UNSIGNED_INT, c.idx.data());
    glDisableClientState(GL_NORMAL_ARRAY); glDisable(GL_LIGHTING);
    if (c.idx.size() < 900000) {                                                                        // the mesh lines of the mathematical shell
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glLineWidth(1.0f); glColor4f(r, g, b, wireA);
        glDrawElements(GL_TRIANGLES, (GLsizei)c.idx.size(), GL_UNSIGNED_INT, c.idx.data()); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    if (d->hideObject && d->showVertices && c.v.size() < 1500000) {                                     // the object is hidden: the wrap shows its vertices (blue)
        glDepthMask(GL_TRUE); glPointSize(c.v.size() > 10000 ? 1.0f : (c.v.size() > 3000 ? 2.0f : 3.0f)); glColor4f(0.15f, 0.42f, 1.0f, 1.0f); glDrawArrays(GL_POINTS, 0, (GLsizei)c.v.size());
    }
    glDisableClientState(GL_VERTEX_ARRAY); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
void col(int h, int i) {                                                                                // axis colours, brighter when hovered / dragged
    static const float C[3][3] = {{0.95f, 0.25f, 0.25f}, {0.30f, 0.85f, 0.35f}, {0.30f, 0.50f, 1.0f}};
    bool on = (g_hover == H_X + i) || (g_drag.mode >= 3 && g_drag.mode <= 6 && g_drag.axis == i);
    float k = on ? 1.0f : 0.78f; glColor3f(std::min(1.0f, C[i][0] * k + (on ? 0.25f : 0)), std::min(1.0f, C[i][1] * k + (on ? 0.25f : 0)), std::min(1.0f, C[i][2] * k + (on ? 0.25f : 0)));
}
void cube(const Vec3& c, float s) {
    static const int F[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    Vec3 p[8]; for (int i = 0; i < 8; i++) p[i] = c + Vec3((i & 1) ? s : -s, (i & 2) ? s : -s, (i & 4) ? s : -s);
    glBegin(GL_QUADS); for (auto& f : F) for (int k = 0; k < 4; k++) glVertex3f(p[f[k]].x, p[f[k]].y, p[f[k]].z); glEnd();
}
void drawGizmo(ModelDoc* d) {
    Tool tool = activeTool(); if (tool != Tool::Move && tool != Tool::Scale && tool != Tool::Rotate) return;
    glClear(GL_DEPTH_BUFFER_BIT); glDisable(GL_LIGHTING); glEnable(GL_LINE_SMOOTH);
    const Vec3 c = d->center(); const float L = gizmoLen(d);
    if (tool == Tool::Move) {
        for (int i = 0; i < 2; i++) {
            Vec3 a = axisDir(d, i, false), tip = c + a * L, u = normalized(cross(a, Vec3(0, 0, 1))), w = cross(a, u);
            col(H_X + i, i); glLineWidth(3.0f); glBegin(GL_LINES); glVertex3f(c.x, c.y, c.z); glVertex3f(tip.x, tip.y, tip.z); glEnd();
            glBegin(GL_TRIANGLE_FAN); glVertex3f(tip.x + a.x * L * 0.22f, tip.y + a.y * L * 0.22f, tip.z + a.z * L * 0.22f);
            for (int k = 0; k <= 16; k++) { float t = 2 * kPi * k / 16; Vec3 q = tip + u * (std::cos(t) * L * 0.07f) + w * (std::sin(t) * L * 0.07f); glVertex3f(q.x, q.y, q.z); }
            glEnd();
        }
    } else if (tool == Tool::Scale) {
        for (int i = 0; i < 3; i++) {
            Vec3 a = axisDir(d, i, true), tip = c + a * L; col(H_X + i, i);
            glLineWidth(3.0f); glBegin(GL_LINES); glVertex3f(c.x, c.y, c.z); glVertex3f(tip.x, tip.y, tip.z); glEnd(); cube(tip, L * 0.06f);
        }
        glColor3f(g_hover == H_ALL ? 1.0f : 0.85f, g_hover == H_ALL ? 1.0f : 0.85f, g_hover == H_ALL ? 0.4f : 0.85f); cube(c, L * 0.07f);
    } else {
        for (int i = 0; i < 3; i++) {
            Vec3 n = axisDir(d, i, false), u = normalized(cross(n, Vec3(0.37f, 0.61f, 0.7f))), w = cross(n, u); col(H_X + i, i); glLineWidth(3.0f);
            glBegin(GL_LINE_LOOP); for (int k = 0; k < 96; k++) { float t = 2 * kPi * k / 96; Vec3 q = c + u * (std::cos(t) * L) + w * (std::sin(t) * L); glVertex3f(q.x, q.y, q.z); } glEnd();
        }
    }
    glLineWidth(1.0f);
}

void render() {
    if (!g_rc) return;
    wglMakeCurrent(g_dc, g_rc);
    RECT rc; GetClientRect(g_view, &rc); const int W = std::max(1, (int)rc.right), H = std::max(1, (int)rc.bottom);
    glViewport(0, 0, W, H); glClearColor(0.17f, 0.19f, 0.24f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ModelDoc* d = activeModel();
    if (d) {
        const Camera& cm = d->cam; Cam k = camOf(cm);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        const float near_ = std::max(cm.dist * 0.01f, 1e-5f), far_ = cm.dist * 60.0f + d->diag() * 4, th = near_ * std::tan(rad(kFov) * 0.5f), asp = (float)W / H;
        glFrustum(-th * asp, th * asp, -th, th, near_, far_);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        const float lp[4] = {0.4f, 0.6f, 1.0f, 0}; glLightfv(GL_LIGHT0, GL_POSITION, lp);                // a light on the camera
        const float la[4] = {0.35f, 0.35f, 0.38f, 1}, ld[4] = {0.85f, 0.85f, 0.85f, 1}; glLightfv(GL_LIGHT0, GL_AMBIENT, la); glLightfv(GL_LIGHT0, GL_DIFFUSE, ld);
        float V[16] = {k.right.x, k.up.x, -k.fwd.x, 0, k.right.y, k.up.y, -k.fwd.y, 0, k.right.z, k.up.z, -k.fwd.z, 0, -dot(k.right, k.eye), -dot(k.up, k.eye), dot(k.fwd, k.eye), 1};
        glMultMatrixf(V);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
        drawGrid(d); if (!d->hideObject) drawObject(d);
        const Tool t = activeTool(); const bool editCurtain = t == Tool::Distance || t == Tool::Precision || t == Tool::Density;
        if (editCurtain || d->showCurtain) drawCurtain(d, editCurtain);
        drawGizmo(d);
    }
    SwapBuffers(g_dc);
}

LRESULT CALLBACK viewProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_view = h; g_dc = GetDC(h);
        PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0};
        int pf = ChoosePixelFormat(g_dc, &pfd); SetPixelFormat(g_dc, pf, &pfd);
        g_rc = wglCreateContext(g_dc); wglMakeCurrent(g_dc, g_rc);
        return 0;
    }
    case WM_SIZE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); render(); EndPaint(h, &ps); return 0; }
    case WM_DESTROY: wglMakeCurrent(nullptr, nullptr); if (g_rc) wglDeleteContext(g_rc); ReleaseDC(h, g_dc); g_rc = nullptr; return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        SetFocus(h); ModelDoc* d = activeModel(); if (!d) return 0;
        RECT rc; GetClientRect(h, &rc); int px = (short)LOWORD(lp), py = (short)HIWORD(lp);
        g_drag = Drag(); g_drag.last = g_drag.start = {px, py};
        if (m == WM_RBUTTONDOWN) g_drag.mode = 1;
        else if (m == WM_MBUTTONDOWN || (wp & MK_SHIFT)) g_drag.mode = 2;
        else if (d->cur.mesh) {
            Tool tool = activeTool(); Vec3 o, dir; ray(d->cam, px, py, rc.right, rc.bottom, o, dir);
            Vec3 ringHit; int hg = hitGizmo(d, px, py, rc.right, rc.bottom, nullptr, &ringHit); g_drag.xf0 = d->cur.xf; g_drag.c0 = d->center();
            if (hg != H_NONE) {
                g_drag.axis = hg - H_X; d->beginEdit();
                if (tool == Tool::Move) { g_drag.mode = 3; g_drag.a0 = axisDir(d, g_drag.axis, false); float dist; axisParam(o, dir, g_drag.c0, g_drag.a0, g_drag.t0, dist); }
                else if (tool == Tool::Scale) {
                    if (hg == H_ALL) { g_drag.mode = 7; }
                    else { g_drag.mode = 5; g_drag.a0 = axisDir(d, g_drag.axis, true); float dist; axisParam(o, dir, g_drag.c0, g_drag.a0, g_drag.t0, dist); if (std::fabs(g_drag.t0) < 1e-6f) g_drag.t0 = gizmoLen(d); }
                } else { g_drag.mode = 6; g_drag.a0 = axisDir(d, g_drag.axis, false); g_drag.p0 = normalized(ringHit - g_drag.c0); }
                SetCapture(h);
            } else { float t; if (rayMesh(d, o, dir, t)) { Vec3 hit = o + dir * t; g_drag.mode = 4; g_drag.planeZ = hit.z; g_drag.p0 = hit; d->beginEdit(); SetCapture(h); } }
        }
        if (g_drag.mode == 1 || g_drag.mode == 2) SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE: {
        ModelDoc* d = activeModel(); if (!d) return 0;
        int px = (short)LOWORD(lp), py = (short)HIWORD(lp); RECT rc; GetClientRect(h, &rc);
        if (g_drag.mode == 0) {
            int hv = d->cur.mesh ? hitGizmo(d, px, py, rc.right, rc.bottom) : H_NONE;
            if (hv != g_hover) { g_hover = hv; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        }
        const int dx = px - g_drag.last.x, dy = py - g_drag.last.y; g_drag.last = {px, py};
        Camera& cm = d->cam;
        if (g_drag.mode == 1) { cm.yaw -= dx * 0.4f; cm.pitch = std::min(89.5f, std::max(-89.5f, cm.pitch + dy * 0.4f)); }
        else if (g_drag.mode == 2) {
            Cam k = camOf(cm); float wpp = 2 * cm.dist * std::tan(rad(kFov) * 0.5f) / std::max(1, (int)rc.bottom);
            cm.target = cm.target - k.right * (dx * wpp) + k.up * (dy * wpp);
        } else {
            Vec3 o, dir; ray(cm, px, py, rc.right, rc.bottom, o, dir); Xform& xf = d->cur.xf; bool changed = false;
            if (g_drag.mode == 3) { float t, dist; if (axisParam(o, dir, g_drag.c0, g_drag.a0, t, dist)) { xf = g_drag.xf0; xf.pos = g_drag.xf0.pos + g_drag.a0 * (t - g_drag.t0); changed = true; } }
            else if (g_drag.mode == 4) { Vec3 hit; if (planeHit(o, dir, Vec3(0, 0, g_drag.planeZ), Vec3(0, 0, 1), hit)) { xf = g_drag.xf0; xf.pos.x += hit.x - g_drag.p0.x; xf.pos.y += hit.y - g_drag.p0.y; changed = true; } }
            else if (g_drag.mode == 5) {
                float t, dist; if (axisParam(o, dir, g_drag.c0, g_drag.a0, t, dist)) {
                    float f = std::max(0.02f, t / g_drag.t0); xf = g_drag.xf0;
                    if (g_uniformScale) xf.scale = g_drag.xf0.scale * f; else xf.scale[g_drag.axis] = g_drag.xf0.scale[g_drag.axis] * f;
                    changed = true;
                }
            } else if (g_drag.mode == 7) { float f = std::max(0.02f, 1.0f - (py - g_drag.start.y) * 0.006f); xf = g_drag.xf0; xf.scale = g_drag.xf0.scale * f; changed = true; }
            else if (g_drag.mode == 6) {
                Vec3 hit; if (planeHit(o, dir, g_drag.c0, g_drag.a0, hit)) {
                    Vec3 v1 = normalized(hit - g_drag.c0); float ang = std::atan2(dot(cross(g_drag.p0, v1), g_drag.a0), dot(g_drag.p0, v1));
                    if (wp & MK_SHIFT) ang = std::round(ang / rad(15.0f)) * rad(15.0f);
                    Mat4 Ra = g_drag.axis == 0 ? Mat4::rotX(ang) : (g_drag.axis == 1 ? Mat4::rotY(ang) : Mat4::rotZ(ang));
                    xf = g_drag.xf0; xf.rotDeg = matEuler(Ra * eulerMat(g_drag.xf0.rotDeg)); changed = true;
                }
            }
            if (changed) { d->rebuild(); d->dropToPlate(); onEditChanged(d, false); }
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        if (g_drag.mode) {
            ModelDoc* d = activeModel(); const int mode = g_drag.mode; g_drag = Drag(); ReleaseCapture();
            if (d && mode >= 3) { d->endEdit(); onEditChanged(d, true); }
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        ModelDoc* d = activeModel(); if (!d) return 0;
        int z = (short)HIWORD(wp); d->cam.dist = std::min(d->diag() * 80, std::max(d->diag() * 0.03f, d->cam.dist * std::pow(0.88f, z / 120.0f)));
        InvalidateRect(h, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONDBLCLK: { ModelDoc* d = activeModel(); if (d) { d->fitCamera(); InvalidateRect(h, nullptr, FALSE); } return 0; }
    }
    return DefWindowProcW(h, m, wp, lp);
}
}  // namespace

void setUniformScale(bool on) { g_uniformScale = on; }

HWND createView(HWND parent, HINSTANCE inst) {
    WNDCLASSW wc = {}; wc.style = CS_OWNDC | CS_DBLCLKS; wc.lpfnWndProc = viewProc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"D3CView";
    RegisterClassW(&wc);
    return CreateWindowExW(0, L"D3CView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 100, 100, parent, nullptr, inst, nullptr);
}
void viewRedraw() { if (g_view) InvalidateRect(g_view, nullptr, FALSE); }
bool viewSnapshot(const std::string& path) {                       // test aid: the current frame as a BMP (only reachable with the D3C_SNAPDIR environment variable)
    if (!g_rc) return false;
    render(); wglMakeCurrent(g_dc, g_rc); RECT rc; GetClientRect(g_view, &rc); const int W = rc.right, H = rc.bottom; if (W <= 0 || H <= 0) return false;
    std::vector<unsigned char> px((size_t)W * H * 4); glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px.data());
    BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + px.size()), 0, 0, 54}; BITMAPINFOHEADER ih = {sizeof ih, W, H, 1, 32, BI_RGB, (DWORD)px.size(), 0, 0, 0, 0};
    FILE* f = _wfopen(std::wstring(path.begin(), path.end()).c_str(), L"wb"); if (!f) return false;
    fwrite(&fh, 1, sizeof fh, f); fwrite(&ih, 1, sizeof ih, f); fwrite(px.data(), 1, px.size(), f); fclose(f); return true;
}
void viewSetPreset(const char* n) {
    ModelDoc* d = activeModel(); if (!d) return; std::string s = n; Camera& c = d->cam;
    if (s == "center") { d->fitCamera(); }
    else {
        c.target = d->center(); float dist = c.dist;
        if (s == "front") { c.yaw = -90; c.pitch = 0; } else if (s == "back") { c.yaw = 90; c.pitch = 0; }
        else if (s == "left") { c.yaw = 180; c.pitch = 0; } else if (s == "right") { c.yaw = 0; c.pitch = 0; }
        else if (s == "top") { c.yaw = -90; c.pitch = 89.5f; } else if (s == "bottom") { c.yaw = -90; c.pitch = -89.5f; }
        c.dist = dist;
    }
    viewRedraw();
}
