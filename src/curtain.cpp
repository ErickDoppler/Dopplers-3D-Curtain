#include "curtain.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

// ================================================================== convex hull (quickhull with conflict lists)
namespace {
struct HFace { int a, b, c; Vec3 n; float d; std::vector<int> out; bool alive = true; };
inline uint64_t ekey(int u, int v) { return ((uint64_t)(uint32_t)u << 32) | (uint32_t)v; }
}

bool convexHull(const std::vector<Vec3>& pts, std::vector<Vec3>& hv, std::vector<uint32_t>& hidx) {
    hv.clear(); hidx.clear();
    const int N = (int)pts.size(); if (N < 4) return false;
    Vec3 lo = pts[0], hi = pts[0]; for (auto& p : pts) { lo = vmin(lo, p); hi = vmax(hi, p); }
    const float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}); if (ext <= 0) return false;
    const float eps = 1e-6f * ext;
    // initial tetrahedron from extreme points
    int ex[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 1; i < N; i++) for (int a = 0; a < 3; a++) { if (pts[i][a] < pts[ex[a * 2]][a]) ex[a * 2] = i; if (pts[i][a] > pts[ex[a * 2 + 1]][a]) ex[a * 2 + 1] = i; }
    int i0 = ex[0], i1 = ex[1]; float best = -1;
    for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) { float d = length(pts[ex[a]] - pts[ex[b]]); if (d > best) { best = d; i0 = ex[a]; i1 = ex[b]; } }
    if (best < eps * 10) return false;
    const Vec3 dir = normalized(pts[i1] - pts[i0]); int i2 = -1; best = -1;
    for (int i = 0; i < N; i++) { Vec3 w = pts[i] - pts[i0]; float d = length(w - dir * dot(w, dir)); if (d > best) { best = d; i2 = i; } }
    if (best < eps * 10) return false;
    const Vec3 nn = normalized(cross(pts[i1] - pts[i0], pts[i2] - pts[i0])); int i3 = -1; best = -1;
    for (int i = 0; i < N; i++) { float d = std::fabs(dot(pts[i] - pts[i0], nn)); if (d > best) { best = d; i3 = i; } }
    if (best < eps * 10) return false;
    std::vector<HFace> F; F.reserve(256); std::unordered_map<uint64_t, int> edge;
    auto addFace = [&](int a, int b, int c) {
        HFace f; f.a = a; f.b = b; f.c = c; f.n = normalized(cross(pts[b] - pts[a], pts[c] - pts[a])); f.d = dot(f.n, pts[a]);
        F.push_back(std::move(f)); int id = (int)F.size() - 1;
        edge[ekey(a, b)] = id; edge[ekey(b, c)] = id; edge[ekey(c, a)] = id; return id; };
    if (dot(nn, pts[i3] - pts[i0]) > 0) std::swap(i1, i2);                                     // the tetrahedron faces must look away from i3
    std::vector<int> pending;
    pending.push_back(addFace(i0, i1, i2)); pending.push_back(addFace(i0, i2, i3)); pending.push_back(addFace(i0, i3, i1)); pending.push_back(addFace(i1, i3, i2));
    // (the four faces above are oriented outward only if i3 is below (i0,i1,i2); verified by the check on the first face)
    for (int p = 0; p < N; p++) {
        if (p == i0 || p == i1 || p == i2 || p == i3) continue;
        for (int f = 0; f < 4; f++) if (dot(F[f].n, pts[p]) - F[f].d > eps) { F[f].out.push_back(p); break; }
    }
    while (!pending.empty()) {
        const int fi = pending.back(); pending.pop_back();
        if (!F[fi].alive || F[fi].out.empty()) continue;
        int farP = -1; float fd = -1;
        for (int p : F[fi].out) { float d = dot(F[fi].n, pts[p]) - F[fi].d; if (d > fd) { fd = d; farP = p; } }
        const Vec3 P = pts[farP];
        std::vector<int> vis, stack{fi}; std::vector<char> seen(F.size(), 0); seen[fi] = 1;
        while (!stack.empty()) {
            int f = stack.back(); stack.pop_back(); vis.push_back(f);
            const int vs[3] = {F[f].a, F[f].b, F[f].c};
            for (int e = 0; e < 3; e++) {
                auto it = edge.find(ekey(vs[(e + 1) % 3], vs[e])); if (it == edge.end()) continue;
                int g = it->second; if (seen[g]) continue;
                if (dot(F[g].n, P) - F[g].d > eps) { seen[g] = 1; stack.push_back(g); }
            }
        }
        std::vector<std::pair<int, int>> horizon; std::vector<int> orphans;
        for (int f : vis) {
            const int vs[3] = {F[f].a, F[f].b, F[f].c};
            for (int e = 0; e < 3; e++) {
                int u = vs[e], v = vs[(e + 1) % 3]; auto it = edge.find(ekey(v, u));
                if (it == edge.end() || !seen[it->second]) horizon.push_back({u, v});
            }
            for (int p : F[f].out) if (p != farP) orphans.push_back(p);
        }
        for (int f : vis) { F[f].alive = false; F[f].out.clear(); F[f].out.shrink_to_fit(); edge.erase(ekey(F[f].a, F[f].b)); edge.erase(ekey(F[f].b, F[f].c)); edge.erase(ekey(F[f].c, F[f].a)); }
        std::vector<int> fresh;
        for (auto& h : horizon) fresh.push_back(addFace(h.first, h.second, farP));
        for (int p : orphans) for (int f : fresh) if (dot(F[f].n, pts[p]) - F[f].d > eps) { F[f].out.push_back(p); break; }
        for (int f : fresh) pending.push_back(f);
    }
    std::vector<int> remap(N, -1);
    for (auto& f : F) {
        if (!f.alive) continue;
        for (int x : {f.a, f.b, f.c}) { if (remap[x] < 0) { remap[x] = (int)hv.size(); hv.push_back(pts[x]); } hidx.push_back((uint32_t)remap[x]); }
    }
    return hidx.size() >= 12;
}

// ================================================================== distance field
namespace {
float distSqPointTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {          // Ericson, Real-Time Collision Detection
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return dot(ap, ap);
    Vec3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return dot(bp, bp);
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { float v = d1 / (d1 - d3); Vec3 q = ap - ab * v; return dot(q, q); }
    Vec3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return dot(cp, cp);
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { float w = d2 / (d2 - d6); Vec3 q = ap - ac * w; return dot(q, q); }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); Vec3 q = p - (b + (c - b) * w); return dot(q, q); }
    float denom = 1.0f / (va + vb + vc), v = vb * denom, w = vc * denom;
    Vec3 q = p - (a + ab * v + ac * w); return dot(q, q);
}

struct Grid { int nx, ny, nz; Vec3 o; float h; size_t at(int i, int j, int k) const { return ((size_t)k * ny + j) * nx + i; } size_t count() const { return (size_t)nx * ny * nz; } };

void seedTriangle(const Grid& G, std::vector<float>& d2, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& A, const Vec3& B, const Vec3& C, float thr2, int depth) {
    Vec3 lo = vmin(a, vmin(b, c)), hi = vmax(a, vmax(b, c));
    int i0 = (int)std::floor((lo.x - G.o.x) / G.h) - 2, i1 = (int)std::floor((hi.x - G.o.x) / G.h) + 3;
    int j0 = (int)std::floor((lo.y - G.o.y) / G.h) - 2, j1 = (int)std::floor((hi.y - G.o.y) / G.h) + 3;
    int k0 = (int)std::floor((lo.z - G.o.z) / G.h) - 2, k1 = (int)std::floor((hi.z - G.o.z) / G.h) + 3;
    i0 = std::max(i0, 0); j0 = std::max(j0, 0); k0 = std::max(k0, 0); i1 = std::min(i1, G.nx - 1); j1 = std::min(j1, G.ny - 1); k1 = std::min(k1, G.nz - 1);
    if (i1 < i0 || j1 < j0 || k1 < k0) return;
    const long long cells = (long long)(i1 - i0 + 1) * (j1 - j0 + 1) * (k1 - k0 + 1);
    if (cells > 600 && depth < 24) {                                                     // a big triangle: split its longest edge, look at the halves separately
        float ab = length(b - a), bc = length(c - b), ca = length(a - c);
        if (ab >= bc && ab >= ca) { Vec3 m = (a + b) * 0.5f; seedTriangle(G, d2, a, m, c, A, B, C, thr2, depth + 1); seedTriangle(G, d2, m, b, c, A, B, C, thr2, depth + 1); }
        else if (bc >= ca) { Vec3 m = (b + c) * 0.5f; seedTriangle(G, d2, a, b, m, A, B, C, thr2, depth + 1); seedTriangle(G, d2, a, m, c, A, B, C, thr2, depth + 1); }
        else { Vec3 m = (c + a) * 0.5f; seedTriangle(G, d2, a, b, m, A, B, C, thr2, depth + 1); seedTriangle(G, d2, m, b, c, A, B, C, thr2, depth + 1); }
        return;
    }
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        Vec3 p(G.o.x + i * G.h, G.o.y + j * G.h, G.o.z + k * G.h);
        float d = distSqPointTri(p, A, B, C);
        if (d < thr2) { float& s = d2[G.at(i, j, k)]; if (d < s) s = d; }
    }
}

// exact Euclidean distance transform (Felzenszwalb-Huttenlocher) of "min over seeds of seedValue + spacing^2 * |index difference|^2", in place
void edtPass(std::vector<float>& f, int n, size_t stride, size_t lines, const std::vector<size_t>& base, float h2) {
    std::vector<int> v(n); std::vector<float> z(n + 1), d(n);
    for (size_t L = 0; L < lines; L++) {
        float* line = f.data() + base[L];
        int k = 0; v[0] = 0; z[0] = -1e30f; z[1] = 1e30f;
        for (int q = 1; q < n; q++) {
            float s;
            for (;;) {
                int p = v[k];
                s = ((line[q * stride] + h2 * q * q) - (line[p * stride] + h2 * p * p)) / (2.0f * h2 * (q - p));
                if (s <= z[k]) { k--; if (k < 0) { k = 0; break; } } else break;
            }
            k++; v[k] = q; z[k] = s; z[k + 1] = 1e30f;
        }
        k = 0;
        for (int q = 0; q < n; q++) { while (z[k + 1] < q) k++; d[q] = h2 * (q - v[k]) * (q - v[k]) + line[v[k] * stride]; }
        for (int q = 0; q < n; q++) line[q * stride] = d[q];
    }
}
}  // namespace

float CurtainField::eval(const Vec3& p) const {
    if (g.empty()) return 1e30f;
    float fx = (p.x - origin.x) / h, fy = (p.y - origin.y) / h, fz = (p.z - origin.z) / h;
    float cx = std::min(std::max(fx, 0.0f), (float)(n[0] - 1)), cy = std::min(std::max(fy, 0.0f), (float)(n[1] - 1)), cz = std::min(std::max(fz, 0.0f), (float)(n[2] - 1));
    float out = h * std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy) + (fz - cz) * (fz - cz));      // outside the box: grows with the distance
    int i = std::min((int)cx, n[0] - 2), j = std::min((int)cy, n[1] - 2), k = std::min((int)cz, n[2] - 2);
    i = std::max(i, 0); j = std::max(j, 0); k = std::max(k, 0);
    float tx = cx - i, ty = cy - j, tz = cz - k;
    auto G = [&](int a, int b, int c) { return g[((size_t)c * n[1] + b) * n[0] + a]; };
    float c00 = G(i, j, k) * (1 - tx) + G(i + 1, j, k) * tx, c10 = G(i, j + 1, k) * (1 - tx) + G(i + 1, j + 1, k) * tx;
    float c01 = G(i, j, k + 1) * (1 - tx) + G(i + 1, j, k + 1) * tx, c11 = G(i, j + 1, k + 1) * (1 - tx) + G(i + 1, j + 1, k + 1) * tx;
    return (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz + out;
}

// ================================================================== surface nets
static void surfaceNets(const CurtainField& F, std::vector<Vec3>& V, std::vector<uint32_t>& I) {
    const int nx = F.n[0], ny = F.n[1], nz = F.n[2];
    if (nx < 2 || ny < 2 || nz < 2) return;
    std::vector<int> cellV((size_t)(nx - 1) * (ny - 1) * nz, -1);
    auto cid = [&](int i, int j, int k) { return ((size_t)k * (ny - 1) + j) * (nx - 1) + i; };
    auto G = [&](int i, int j, int k) { return F.g[((size_t)k * ny + j) * nx + i]; };
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int k = 0; k < nz - 1; k++) for (int j = 0; j < ny - 1; j++) for (int i = 0; i < nx - 1; i++) {
        float c[8]; int mask = 0;
        for (int q = 0; q < 8; q++) { c[q] = G(i + (q & 1), j + ((q >> 1) & 1), k + (q >> 2)); if (c[q] < 0) mask |= 1 << q; }
        if (mask == 0 || mask == 255) continue;
        Vec3 sum; int cnt = 0;
        for (int e = 0; e < 12; e++) {
            int a = E[e][0], b = E[e][1]; if (((mask >> a) & 1) == ((mask >> b) & 1)) continue;
            float t = c[a] / (c[a] - c[b]);
            Vec3 pa((float)(a & 1), (float)((a >> 1) & 1), (float)(a >> 2)), pb((float)(b & 1), (float)((b >> 1) & 1), (float)(b >> 2));
            sum += pa + (pb - pa) * t; cnt++;
        }
        sum = sum / (float)cnt;
        cellV[cid(i, j, k)] = (int)V.size();
        V.push_back(Vec3(F.origin.x + (i + sum.x) * F.h, F.origin.y + (j + sum.y) * F.h, F.origin.z + (k + sum.z) * F.h));
    }
    auto quad = [&](int a, int b, int c, int d, bool flip) {
        if (a < 0 || b < 0 || c < 0 || d < 0) return;
        if (flip) { std::swap(b, d); }
        I.insert(I.end(), {(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)a, (uint32_t)c, (uint32_t)d});
    };
    for (int k = 1; k < nz - 1; k++) for (int j = 1; j < ny - 1; j++) for (int i = 0; i < nx - 1; i++) {      // edges along x
        float g0 = G(i, j, k), g1 = G(i + 1, j, k); if ((g0 < 0) == (g1 < 0)) continue;
        quad(cellV[cid(i, j - 1, k - 1)], cellV[cid(i, j, k - 1)], cellV[cid(i, j, k)], cellV[cid(i, j - 1, k)], !(g0 < 0));
    }
    for (int k = 1; k < nz - 1; k++) for (int j = 0; j < ny - 1; j++) for (int i = 1; i < nx - 1; i++) {      // along y
        float g0 = G(i, j, k), g1 = G(i, j + 1, k); if ((g0 < 0) == (g1 < 0)) continue;
        quad(cellV[cid(i - 1, j, k - 1)], cellV[cid(i - 1, j, k)], cellV[cid(i, j, k)], cellV[cid(i, j, k - 1)], !(g0 < 0));
    }
    for (int k = 0; k < nz - 1; k++) for (int j = 1; j < ny - 1; j++) for (int i = 1; i < nx - 1; i++) {      // along z
        float g0 = G(i, j, k), g1 = G(i, j, k + 1); if ((g0 < 0) == (g1 < 0)) continue;
        quad(cellV[cid(i - 1, j - 1, k)], cellV[cid(i, j - 1, k)], cellV[cid(i, j, k)], cellV[cid(i - 1, j, k)], !(g0 < 0));
    }
}

// ================================================================== wrap density: more or fewer vertices on the same surface
static void projectToSurface(const CurtainField& F, Vec3& p, int iters) {                        // Newton steps on the field: onto the zero level
    const float e = F.h * 0.5f;
    for (int k = 0; k < iters; k++) {
        const float g = F.eval(p);
        Vec3 gr(F.eval(p + Vec3(e, 0, 0)) - F.eval(p - Vec3(e, 0, 0)), F.eval(p + Vec3(0, e, 0)) - F.eval(p - Vec3(0, e, 0)), F.eval(p + Vec3(0, 0, e)) - F.eval(p - Vec3(0, 0, e)));
        gr = gr / (2.0f * e);                                                                       // a real gradient (the difference spans 2e)
        const float l2 = dot(gr, gr); if (l2 < 1e-12f) break;
        Vec3 step = gr * (g / l2); const float sl = length(step); if (sl > F.h) step = step * (F.h / sl);
        p -= step;
    }
}
static void subdivideOnce(const CurtainField& F, std::vector<Vec3>& V, std::vector<uint32_t>& I) {
    std::unordered_map<uint64_t, uint32_t> mid; std::vector<uint32_t> J; J.reserve(I.size() * 4);
    auto midpoint = [&](uint32_t a, uint32_t b) {
        const uint64_t k = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a; auto it = mid.find(k); if (it != mid.end()) return it->second;
        Vec3 p = (V[a] + V[b]) * 0.5f; projectToSurface(F, p, 4); const uint32_t id = (uint32_t)V.size(); V.push_back(p); mid[k] = id; return id; };
    for (size_t t = 0; t + 2 < I.size(); t += 3) {
        const uint32_t a = I[t], b = I[t + 1], c = I[t + 2], ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
        J.insert(J.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
    }
    I.swap(J);
}
// shortest-edge-first edge collapse that keeps the surface closed: the link condition (the two ends share exactly two neighbours) and a check against folded triangles
static void decimateTo(const CurtainField& F, std::vector<Vec3>& V, std::vector<uint32_t>& I, size_t target) {
    const size_t nv = V.size(), nt = I.size() / 3;
    std::vector<std::array<uint32_t, 3>> T(nt); for (size_t t = 0; t < nt; t++) T[t] = {I[t * 3], I[t * 3 + 1], I[t * 3 + 2]};
    std::vector<char> tAlive(nt, 1), vAlive(nv, 1); std::vector<std::vector<uint32_t>> vt(nv); std::vector<uint32_t> ver(nv, 0);
    for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) vt[T[t][k]].push_back((uint32_t)t);
    struct E { float len; uint32_t u, v, vu, vv; bool operator<(const E& o) const { return len > o.len; } };
    std::priority_queue<E> pq;
    auto push = [&](uint32_t u, uint32_t v) { pq.push({length(V[u] - V[v]), u, v, ver[u], ver[v]}); };
    for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) { uint32_t a = T[t][k], b = T[t][(k + 1) % 3]; if (a < b) push(a, b); }   // each undirected edge once (b > a on one of its two faces)
    auto liveTris = [&](uint32_t v, std::vector<uint32_t>& out) {                                    // alive triangles around v (also tidies the list)
        out.clear(); auto& L = vt[v]; size_t w = 0;
        for (uint32_t t : L) if (tAlive[t] && (T[t][0] == v || T[t][1] == v || T[t][2] == v)) { bool dup = false; for (size_t q = 0; q < w; q++) if (L[q] == t) { dup = true; break; } if (!dup) L[w++] = t; }
        L.resize(w); out = L; };
    size_t alive = nv; std::vector<uint32_t> tu, tv;
    while (alive > target && !pq.empty()) {
        E e = pq.top(); pq.pop();
        if (!vAlive[e.u] || !vAlive[e.v] || ver[e.u] != e.vu || ver[e.v] != e.vv) continue;
        liveTris(e.u, tu); liveTris(e.v, tv);
        std::vector<uint32_t> shared; for (uint32_t t : tu) if (T[t][0] == e.v || T[t][1] == e.v || T[t][2] == e.v) shared.push_back(t);
        if (shared.size() != 2) continue;
        std::set<uint32_t> nu, nvv; for (uint32_t t : tu) for (int k = 0; k < 3; k++) if (T[t][k] != e.u) nu.insert(T[t][k]);
        for (uint32_t t : tv) for (int k = 0; k < 3; k++) if (T[t][k] != e.v) nvv.insert(T[t][k]);
        int common = 0; for (uint32_t x : nu) if (nvv.count(x)) common++;
        if (common != 2) continue;                                                                   // would pinch the surface
        Vec3 m = (V[e.u] + V[e.v]) * 0.5f; projectToSurface(F, m, 2);
        bool ok = true;
        auto flips = [&](const std::vector<uint32_t>& ts, uint32_t from) {
            for (uint32_t t : ts) {
                if (t == shared[0] || t == shared[1]) continue;
                Vec3 p[3]; for (int k = 0; k < 3; k++) p[k] = T[t][k] == from ? m : V[T[t][k]];
                Vec3 on = cross(V[T[t][1]] - V[T[t][0]], V[T[t][2]] - V[T[t][0]]), nn = cross(p[1] - p[0], p[2] - p[0]);
                if (dot(on, nn) <= 0.2f * length(on) * length(nn) || length(nn) < 1e-12f) return true;
            } return false; };
        if (flips(tu, e.u) || flips(tv, e.v)) ok = false;
        if (!ok) continue;
        V[e.u] = m; vAlive[e.v] = 0; alive--; ver[e.u]++; ver[e.v]++;
        for (uint32_t t : shared) tAlive[t] = 0;
        for (uint32_t t : tv) if (tAlive[t]) { for (int k = 0; k < 3; k++) if (T[t][k] == e.v) T[t][k] = e.u; vt[e.u].push_back(t); }
        liveTris(e.u, tu); std::set<uint32_t> ns; for (uint32_t t : tu) for (int k = 0; k < 3; k++) if (T[t][k] != e.u) ns.insert(T[t][k]);
        for (uint32_t x : ns) push(std::min(e.u, x), std::max(e.u, x));
    }
    std::vector<uint32_t> remap(nv, 0xFFFFFFFFu); std::vector<Vec3> NV; std::vector<uint32_t> NI;
    for (size_t t = 0; t < nt; t++) if (tAlive[t]) for (int k = 0; k < 3; k++) { uint32_t x = T[t][k]; if (remap[x] == 0xFFFFFFFFu) { remap[x] = (uint32_t)NV.size(); NV.push_back(V[x]); } NI.push_back(remap[x]); }
    V.swap(NV); I.swap(NI);
}static void remeshToCount(const CurtainField& F, std::vector<Vec3>& V, std::vector<uint32_t>& I, size_t target) {
    if (V.size() < 8 || I.empty()) return;
    for (auto& p : V) projectToSurface(F, p, 3);                                      // the surface-nets vertices are only near the surface: put them on it
    int levels = 0;
    while (V.size() < target && levels < 8 && V.size() * 4 <= 8000000) { subdivideOnce(F, V, I); levels++; }
    if (V.size() > target + target / 20) decimateTo(F, V, I, target);
}

// ================================================================== the curtain
bool computeCurtain(const std::vector<Vec3>& wv, const std::vector<uint32_t>& widx, const CurtainParams& prm, CurtainResult& out, const std::atomic<bool>* cancel) {
    const auto t0 = std::chrono::steady_clock::now();
    auto cancelled = [&] { return cancel && cancel->load(); };
    out = CurtainResult();
    if (wv.size() < 4 || widx.size() < 3) return false;
    Vec3 olo = wv[0], ohi = wv[0]; for (auto& p : wv) { olo = vmin(olo, p); ohi = vmax(ohi, p); }
    const float diag = length(ohi - olo); if (diag <= 0) return false;
    const float dist = std::max(0.0f, prm.distance) * diag;

    const std::vector<Vec3>* GV = &wv; const std::vector<uint32_t>* GI = &widx;
    if (prm.mode == CurtainMode::Hull) {
        if (!convexHull(wv, out.hullV, out.hullIdx)) {                                           // flat or degenerate object: use its bounding box
            out.hullV = {{olo.x, olo.y, olo.z}, {ohi.x, olo.y, olo.z}, {ohi.x, ohi.y, olo.z}, {olo.x, ohi.y, olo.z}, {olo.x, olo.y, ohi.z}, {ohi.x, olo.y, ohi.z}, {ohi.x, ohi.y, ohi.z}, {olo.x, ohi.y, ohi.z}};
            out.hullIdx = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3};
        }
        GV = &out.hullV; GI = &out.hullIdx;
    }
    if (cancelled()) return false;
    const Vec3 center = (olo + ohi) * 0.5f; float R0 = 0; for (auto& p : wv) R0 = std::max(R0, length(p - center));
    // the region that must be covered. Hull: the hull grown by the distance. Smooth: the surface lies within dist + 2 t R0 of the object (t = how far the blend towards the sphere has gone)
    const float tBlend = (prm.mode == CurtainMode::Smooth && dist > 0) ? dist / (dist + R0) : 0.0f;
    const float margin = dist + (prm.mode == CurtainMode::Smooth ? 2.0f * tBlend * R0 : 0.0f);
    Vec3 rlo = olo - Vec3(margin, margin, margin), rhi = ohi + Vec3(margin, margin, margin);
    if (prm.mode == CurtainMode::Smooth) { const Vec3 sl = center - Vec3(R0 + dist, R0 + dist, R0 + dist), sh = center + Vec3(R0 + dist, R0 + dist, R0 + dist); rlo = vmax(rlo, sl); rhi = vmin(rhi, sh); }
    const int N = std::max(kMinPrecision / 2, std::min(kMaxPrecision + 16, prm.precision));
    const float size = std::max({rhi.x - rlo.x, rhi.y - rlo.y, rhi.z - rlo.z});
    const float h = size / (float)(N - 4);
    const Vec3 pad(2 * h, 2 * h, 2 * h);
    Grid G; G.h = h; G.o = rlo - pad;
    Vec3 hiG = rhi + pad;
    G.nx = (int)std::ceil((hiG.x - G.o.x) / h) + 1; G.ny = (int)std::ceil((hiG.y - G.o.y) / h) + 1; G.nz = (int)std::ceil((hiG.z - G.o.z) / h) + 1;
    const size_t total = G.count(); if (total > 90000000ull) return false;
    std::vector<float> d2(total, 1e20f);
    const float thr = 1.75f * h, thr2 = thr * thr;
    for (size_t t = 0; t + 2 < GI->size(); t += 3) {
        if ((t & 0x3FFF) == 0 && cancelled()) return false;
        const Vec3 &A = (*GV)[(*GI)[t]], &B = (*GV)[(*GI)[t + 1]], &C = (*GV)[(*GI)[t + 2]];
        seedTriangle(G, d2, A, B, C, A, B, C, thr2, 0);
    }
    if (cancelled()) return false;
    // inside / outside: flood the empty space from the corner; surface voxels (closer than 0.9 h to a triangle) block it
    const float blk2 = (0.9f * h) * (0.9f * h);
    std::vector<uint8_t> outside(total, 0);
    { std::vector<uint32_t> st; st.push_back(0); outside[0] = 1;
      while (!st.empty()) {
          uint32_t c = st.back(); st.pop_back();
          int i = (int)(c % G.nx), j = (int)((c / G.nx) % G.ny), k = (int)(c / ((size_t)G.nx * G.ny));
          const int nb[6][3] = {{i - 1, j, k}, {i + 1, j, k}, {i, j - 1, k}, {i, j + 1, k}, {i, j, k - 1}, {i, j, k + 1}};
          for (auto& q : nb) {
              if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] >= G.nx || q[1] >= G.ny || q[2] >= G.nz) continue;
              size_t id = G.at(q[0], q[1], q[2]); if (outside[id] || d2[id] < blk2) continue;
              outside[id] = 1; st.push_back((uint32_t)id);
          }
      } }
    for (size_t id = 0; id < total; id++) if (!outside[id] && d2[id] < blk2) outside[id] = 2;       // surface voxels: neither inside nor outside, their distance is about zero
    if (cancelled()) return false;
    // distance transform of the seeds: three separable passes
    const float h2 = h * h; std::vector<size_t> base;
    base.clear(); for (int k = 0; k < G.nz; k++) for (int j = 0; j < G.ny; j++) base.push_back(G.at(0, j, k)); edtPass(d2, G.nx, 1, base.size(), base, h2);
    if (cancelled()) return false;
    base.clear(); for (int k = 0; k < G.nz; k++) for (int i = 0; i < G.nx; i++) base.push_back(G.at(i, 0, k)); edtPass(d2, G.ny, (size_t)G.nx, base.size(), base, h2);
    if (cancelled()) return false;
    base.clear(); for (int j = 0; j < G.ny; j++) for (int i = 0; i < G.nx; i++) base.push_back(G.at(i, j, 0)); edtPass(d2, G.nz, (size_t)G.nx * G.ny, base.size(), base, h2);
    if (cancelled()) return false;
    CurtainField& F = out.field;
    F.n[0] = G.nx; F.n[1] = G.ny; F.n[2] = G.nz; F.origin = G.o; F.h = h; F.level = dist; F.g.resize(total);
    const float t = tBlend;                                                                                     // how far the blend has gone towards the sphere
    for (int k = 0; k < G.nz; k++) for (int j = 0; j < G.ny; j++) for (int i = 0; i < G.nx; i++) {
        size_t id = G.at(i, j, k);
        float s = std::sqrt(std::max(0.0f, d2[id])); if (outside[id] == 0) s = -s;
        float f = s;
        if (t > 0) { Vec3 p(G.o.x + i * h, G.o.y + j * h, G.o.z + k * h); f = (1 - t) * s + t * (length(p - center) - R0); }
        F.g[id] = f - dist;
    }
    surfaceNets(F, out.v, out.idx);
    if (cancelled()) return false;
    remeshToCount(F, out.v, out.idx, (size_t)std::max(kMinDensity, std::min(kMaxDensity, prm.density)));
    out.nrm.resize(out.v.size());
    for (size_t i = 0; i < out.v.size(); i++) {
        const Vec3& p = out.v[i]; const float e = h * 0.5f;
        Vec3 gr(F.eval(p + Vec3(e, 0, 0)) - F.eval(p - Vec3(e, 0, 0)), F.eval(p + Vec3(0, e, 0)) - F.eval(p - Vec3(0, e, 0)), F.eval(p + Vec3(0, 0, e)) - F.eval(p - Vec3(0, 0, e)));
        out.nrm[i] = normalized(gr);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

// ================================================================== exports for the engine
static float quantRange(const CurtainField& f) { return 16.0f * f.h; }

std::string curtainHeaderText(const CurtainField& f, const std::string& name) {
    std::string up; for (char c : name) up.push_back((char)toupper((unsigned char)c));
    const float range = quantRange(f), scale = range / 32767.0f;
    std::string s; char b[256];
    s += "// Dopplers 3D Curtain - generated. Signed distance to the curtain surface: negative inside, zero on the curtain, positive outside.\n"
         "// One trilinear lookup (8 reads) per query. Values are 16-bit, clamped to +-" + std::to_string(range) + " around the surface.\n#pragma once\n#include <stdint.h>\n#include <math.h>\n";
    snprintf(b, sizeof b, "#define %s_NX %d\n#define %s_NY %d\n#define %s_NZ %d\n", up.c_str(), f.n[0], up.c_str(), f.n[1], up.c_str(), f.n[2]); s += b;
    snprintf(b, sizeof b, "static const float %s_ORIGIN[3] = {%.9gf, %.9gf, %.9gf};\nstatic const float %s_CELL = %.9gf;\nstatic const float %s_SCALE = %.9gf;\n", name.c_str(), f.origin.x, f.origin.y, f.origin.z, name.c_str(), f.h, name.c_str(), scale); s += b;
    snprintf(b, sizeof b, "static const int16_t %s_DATA[%zu] = {", name.c_str(), f.g.size()); s += b;
    for (size_t i = 0; i < f.g.size(); i++) {
        if (i % 24 == 0) s += "\n";
        int q = (int)std::lround(std::min(std::max(f.g[i], -range), range) / scale);
        snprintf(b, sizeof b, "%d,", q); s += b;
    }
    s += "\n};\n";
    snprintf(b, sizeof b, "static inline float %s_eval(float x, float y, float z) {\n", name.c_str()); s += b;
    snprintf(b, sizeof b, "    float fx = (x - %s_ORIGIN[0]) / %s_CELL, fy = (y - %s_ORIGIN[1]) / %s_CELL, fz = (z - %s_ORIGIN[2]) / %s_CELL;\n", name.c_str(), name.c_str(), name.c_str(), name.c_str(), name.c_str(), name.c_str()); s += b;
    snprintf(b, sizeof b, "    float cx = fx < 0 ? 0 : (fx > %s_NX - 1 ? %s_NX - 1 : fx), cy = fy < 0 ? 0 : (fy > %s_NY - 1 ? %s_NY - 1 : fy), cz = fz < 0 ? 0 : (fz > %s_NZ - 1 ? %s_NZ - 1 : fz);\n", up.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str()); s += b;
    snprintf(b, sizeof b, "    float outside = %s_CELL * sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy) + (fz - cz) * (fz - cz));\n", name.c_str()); s += b;
    snprintf(b, sizeof b, "    int i = (int)cx, j = (int)cy, k = (int)cz; if (i > %s_NX - 2) i = %s_NX - 2; if (j > %s_NY - 2) j = %s_NY - 2; if (k > %s_NZ - 2) k = %s_NZ - 2;\n", up.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str()); s += b;
    s += "    float tx = cx - i, ty = cy - j, tz = cz - k;\n";
    snprintf(b, sizeof b, "    const int16_t* d = %s_DATA + ((k * %s_NY + j) * %s_NX + i); const int sx = 1, sy = %s_NX, sz = %s_NX * %s_NY;\n", name.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str(), up.c_str()); s += b;
    s += "    float c00 = d[0] * (1 - tx) + d[sx] * tx, c10 = d[sy] * (1 - tx) + d[sy + sx] * tx, c01 = d[sz] * (1 - tx) + d[sz + sx] * tx, c11 = d[sz + sy] * (1 - tx) + d[sz + sy + sx] * tx;\n";
    snprintf(b, sizeof b, "    return ((c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz) * %s_SCALE + outside;\n}\n", name.c_str()); s += b;
    return s;
}

std::string curtainBlob(const CurtainField& f) {
    const float range = quantRange(f), scale = range / 32767.0f;
    std::string o("CVF1"); int32_t n[3] = {f.n[0], f.n[1], f.n[2]}; float hdr[5] = {f.origin.x, f.origin.y, f.origin.z, f.h, scale};
    o.append((const char*)n, 12); o.append((const char*)hdr, 20);
    o.reserve(o.size() + f.g.size() * 2);
    for (float v : f.g) { int16_t q = (int16_t)std::lround(std::min(std::max(v, -range), range) / scale); o.append((const char*)&q, 2); }
    return o;
}
bool curtainFieldFromBlob(const std::string& b, CurtainField& f) {
    if (b.size() < 36 || memcmp(b.data(), "CVF1", 4)) return false;
    int32_t n[3]; float hdr[5]; memcpy(n, b.data() + 4, 12); memcpy(hdr, b.data() + 16, 20);
    const size_t cnt = (size_t)n[0] * n[1] * n[2];
    if (n[0] < 2 || n[1] < 2 || n[2] < 2 || b.size() != 36 + cnt * 2) return false;
    f.n[0] = n[0]; f.n[1] = n[1]; f.n[2] = n[2]; f.origin = Vec3(hdr[0], hdr[1], hdr[2]); f.h = hdr[3]; f.g.resize(cnt);
    for (size_t i = 0; i < cnt; i++) { int16_t q; memcpy(&q, b.data() + 36 + i * 2, 2); f.g[i] = q * hdr[4]; }
    return true;
}

void extractSurface(const CurtainField& f, std::vector<Vec3>& v, std::vector<uint32_t>& idx) { v.clear(); idx.clear(); surfaceNets(f, v, idx); }
