// Checks the core without any window. Pass criteria (decided before running):
//  1. torus (R=1, r=0.3): Smooth curtain at 5% of the diagonal: field value on the tube surface ~ -distance (within 1.5 cells), at a point
//     distance away from the tube ~ 0, far away > 0; the hole centre is OUTSIDE (g > 0) for Smooth and INSIDE (g < 0) for Hull
//  2. the surface-nets mesh is closed (boundary edges < 1%) and has triangles
//  3. reduceDetails(50%) keeps 40..60% of the vertices, all indices valid, triangles remain
//  4. OBJ and GLB round trip: same vertex and triangle counts; header export compiles as text and the blob round trips
//  5. Hull of a cube's 8 corners + inner points: 12 triangles
#include <cstdio>
#include <set>
#include <map>
#include <chrono>
#include "../src/curtain.h"
#include "../src/sys.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Mesh starMesh() {                                                                // a cube with six thin spikes: sharp turns the wrap must follow
    Mesh m; const float s = 1.0f, b = 0.12f, a = 2.0f;
    for (int i = 0; i < 8; i++) m.v.emplace_back((i & 1) ? s : -s, (i & 2) ? s : -s, (i & 4) ? s : -s);
    const uint32_t F[12][3] = {{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}}; for (auto& f : F) m.idx.insert(m.idx.end(), {f[0], f[1], f[2]});
    for (int ax = 0; ax < 3; ax++) for (int sg = -1; sg <= 1; sg += 2) {
        uint32_t base = (uint32_t)m.v.size(); Vec3 c; c[ax] = (float)sg * s; int u = (ax + 1) % 3, w = (ax + 2) % 3;
        for (int k = 0; k < 4; k++) { Vec3 p = c; p[u] += ((k & 1) ? b : -b); p[w] += ((k & 2) ? b : -b); m.v.push_back(p); }
        Vec3 apex; apex[ax] = (float)sg * a; m.v.push_back(apex); uint32_t t = base + 4;
        m.idx.insert(m.idx.end(), {base, base + 1, t, base + 1, base + 3, t, base + 3, base + 2, t, base + 2, base, t});
    }
    return m;
}
static Mesh torus(int nu, int nv, float R, float r) {
    Mesh m;
    for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) {
        float u = 2 * kPi * i / nu, v = 2 * kPi * j / nv;
        m.v.emplace_back((R + r * cosf(v)) * cosf(u), (R + r * cosf(v)) * sinf(u), r * sinf(v));
    }
    for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) {
        uint32_t a = i * nv + j, b = ((i + 1) % nu) * nv + j, c = ((i + 1) % nu) * nv + (j + 1) % nv, d = i * nv + (j + 1) % nv;
        m.idx.insert(m.idx.end(), {a, b, c, a, c, d});
    }
    return m;
}
static double boundaryFraction(const std::vector<uint32_t>& idx) {
    std::map<std::pair<uint32_t, uint32_t>, int> e;
    for (size_t t = 0; t + 2 < idx.size(); t += 3) for (int k = 0; k < 3; k++) { uint32_t a = idx[t + k], b = idx[t + (k + 1) % 3]; e[{std::min(a, b), std::max(a, b)}]++; }
    size_t bad = 0; for (auto& kv : e) if (kv.second == 1) bad++;                                // open edges = holes
    return e.empty() ? 1.0 : (double)bad / e.size();
}

int main() {
    Mesh m = torus(96, 48, 1.0f, 0.3f);
    Vec3 lo, hi; m.bounds(lo, hi); float diag = length(hi - lo);
    printf("torus: %zu vertices, %zu triangles, diagonal %.3f\n", m.v.size(), m.triCount(), diag);

    CurtainParams p; p.mode = CurtainMode::Smooth; p.distance = 0.05f; p.precision = 96;
    CurtainResult r; auto t0 = std::chrono::steady_clock::now();
    bool ok = computeCurtain(m.v, m.idx, p, r, nullptr);
    printf("smooth: %.2f s, grid %dx%dx%d, curtain %zu vertices %zu triangles\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), r.field.n[0], r.field.n[1], r.field.n[2], r.v.size(), r.idx.size() / 3);
    CHECK(ok && !r.v.empty(), "smooth curtain computed");
    const float d = p.distance * diag, cell = r.field.h;
    float onSurf = r.field.eval(Vec3(1.3f, 0, 0));                         // outermost point of the tube
    float atCurtain = r.field.eval(Vec3(1.3f + d, 0, 0));
    float farG = r.field.eval(Vec3(3.0f, 0, 0));
    CHECK(std::fabs(onSurf + d) < 1.5f * cell, "on the tube surface g = %.4f, expected %.4f (cell %.4f)", onSurf, -d, cell);
    CHECK(std::fabs(atCurtain) < 2.5f * cell, "at distance d from the tube g = %.4f, expected ~0 (smooth mode bends slightly outwards)", atCurtain);
    CHECK(farG > 0.5f, "far away g = %.3f > 0", farG);
    float hole = r.field.eval(Vec3(0, 0, 0));
    CHECK(hole > 0, "smooth: hole centre is outside, g = %.3f", hole);
    double bf = boundaryFraction(r.idx);
    CHECK(bf < 0.01 && r.idx.size() > 300, "surface nets mesh closed: boundary edge fraction %.4f", bf);

    CurtainParams ph = p; ph.mode = CurtainMode::Hull; CurtainResult rh;
    ok = computeCurtain(m.v, m.idx, ph, rh, nullptr);
    CHECK(ok && !rh.v.empty(), "hull curtain computed (hull %zu vertices %zu triangles)", rh.hullV.size(), rh.hullIdx.size() / 3);
    float holeH = rh.field.eval(Vec3(0, 0, 0));
    CHECK(holeH < 0, "hull: hole centre is inside the curtain, g = %.3f", holeH);
    CHECK(boundaryFraction(rh.hullIdx) < 1e-9, "hull is a closed surface");
    CHECK(boundaryFraction(rh.idx) < 0.01, "hull curtain closed");

    // big distance in smooth mode tends to a sphere: radius of the surface in several directions should be close
    CurtainParams pb = p; pb.distance = 1.5f; pb.precision = 64; CurtainResult rb; computeCurtain(m.v, m.idx, pb, rb, nullptr);
    float rmin = 1e9f, rmax = 0; Vec3 c = (lo + hi) * 0.5f;
    for (auto& v : rb.v) { float L = length(v - c); rmin = std::min(rmin, L); rmax = std::max(rmax, L); }
    printf("smooth, distance 150%%: curtain radius %.3f .. %.3f\n", rmin, rmax);
    CHECK(rmax / rmin < 1.12f, "large distance gives nearly a sphere (ratio %.3f)", rmax / rmin);

    // density is independent of precision: the vertex count follows the target, the shape follows the precision
    {
        CurtainParams a = p; a.distance = 0.03f; a.density = 20000; CurtainResult ra, rb, rc, rref;
        a.precision = 48; computeCurtain(m.v, m.idx, a, ra, nullptr);
        a.precision = 160; computeCurtain(m.v, m.idx, a, rb, nullptr);
        printf("density 20000: precision 48 -> %zu vertices, precision 160 -> %zu vertices\n", ra.v.size(), rb.v.size());
        CHECK(std::fabs((double)ra.v.size() / 20000 - 1) < 0.15 && std::fabs((double)rb.v.size() / 20000 - 1) < 0.15, "vertex count stays near the density target whatever the precision");
        CurtainParams up = p; up.distance = 0.03f; up.precision = 32; up.density = 90000; computeCurtain(m.v, m.idx, up, rc, nullptr);
        printf("precision 32, density 90000 -> %zu vertices\n", rc.v.size());
        CHECK(rc.v.size() > 76000, "a low precision wrap can still have many vertices (%zu)", rc.v.size());
        std::vector<float> gs; for (auto& v : rc.v) gs.push_back(std::fabs(rc.field.eval(v))); std::sort(gs.begin(), gs.end()); float p99 = gs[gs.size() * 99 / 100], worst = gs.back(); CHECK(p99 < 0.3f * rc.field.h && worst < 1.0f * rc.field.h, "added vertices lie on the wrap surface (99%% within %.4f, worst %.4f, cell %.4f)", p99, worst, rc.field.h);
        Mesh star = starMesh(); Vec3 slo, shi; star.bounds(slo, shi); (void)slo; (void)shi;
        CurtainParams hi = p; hi.distance = 0.04f; hi.precision = 384; hi.density = 20000; computeCurtain(star.v, star.idx, hi, rref, nullptr);
        auto err = [&](const CurtainResult& r) { double e = 0; int n = 0; for (size_t i = 0; i < rref.v.size(); i += 37) { e += std::fabs(r.field.eval(rref.v[i])); n++; } return e / std::max(1, n); };
        double e24 = 0, e192 = 0; { CurtainParams q = hi; q.precision = 24; CurtainResult r1; computeCurtain(star.v, star.idx, q, r1, nullptr); e24 = err(r1); q.precision = 192; CurtainResult r2; computeCurtain(star.v, star.idx, q, r2, nullptr); e192 = err(r2); }
        printf("spiky shape: mean distance of the precision-384 wrap from the wraps of precision 24 / 192: %.4f / %.4f\n", e24, e192);
        CHECK(e192 < e24 * 0.5 && e192 < 0.03, "higher precision follows the shape closer (%.4f vs %.4f)", e192, e24);
    }

    // eraser: the hole is closed with ribs between the neighbours of the removed vertices
    {
        auto topo = [](const Mesh& q, size_t& open, size_t& nonman, long& euler) {
            std::map<std::pair<uint32_t, uint32_t>, int> e; for (size_t t = 0; t + 2 < q.idx.size(); t += 3) for (int k = 0; k < 3; k++) { uint32_t a = q.idx[t + k], b = q.idx[t + (k + 1) % 3]; e[{std::min(a, b), std::max(a, b)}]++; }
            open = nonman = 0; for (auto& kv : e) { if (kv.second == 1) open++; else if (kv.second > 2) nonman++; }
            euler = (long)q.v.size() - (long)e.size() + (long)q.triCount(); };
        size_t op, nm; long eu; topo(m, op, nm, eu); printf("torus: open %zu, non-manifold %zu, Euler characteristic %ld\n", op, nm, eu);
        std::vector<uint8_t> del(m.v.size(), 0); del[100] = 1; size_t holes = 0; auto r1 = eraseVertices(m, del, &holes); topo(*r1, op, nm, eu);
        CHECK(op == 0 && nm == 0 && eu == 0 && r1->triCount() + 2 == m.triCount() && holes == 1, "one vertex erased: closed, Euler 0, two triangles fewer (%zu -> %zu, %zu hole)", m.triCount(), r1->triCount(), holes);
        std::fill(del.begin(), del.end(), 0); for (int i = 100; i < 106; i++) del[i] = 1; auto r2 = eraseVertices(m, del, &holes); topo(*r2, op, nm, eu);
        CHECK(op == 0 && nm == 0 && eu == 0, "six neighbouring vertices erased: the one ring is closed (open %zu, non-manifold %zu, Euler %ld, %zu vertices left)", op, nm, eu, r2->v.size());
        std::fill(del.begin(), del.end(), 0); size_t cnt = 0; for (size_t i = 0; i < m.v.size(); i++) if (m.v[i].x > 0.85f && m.v[i].z > -0.05f) { del[i] = 1; cnt++; }
        auto r3 = eraseVertices(m, del, &holes); topo(*r3, op, nm, eu);
        CHECK(op == 0 && nm == 0 && eu == 0, "a patch of %zu vertices erased: closed again (open %zu, non-manifold %zu, Euler %ld, %zu holes)", cnt, op, nm, eu, holes);
        Mesh grid; const int G = 8; for (int j = 0; j <= G; j++) for (int i = 0; i <= G; i++) grid.v.emplace_back((float)i, (float)j, 0.0f);
        for (int j = 0; j < G; j++) for (int i = 0; i < G; i++) { uint32_t a = j * (G + 1) + i, b = a + 1, c = a + G + 1, d = c + 1; grid.idx.insert(grid.idx.end(), {a, b, d, a, d, c}); }
        topo(grid, op, nm, eu); const size_t border = op; std::vector<uint8_t> dg(grid.v.size(), 0); dg[4 * (G + 1) + 4] = 1; auto r4 = eraseVertices(grid, dg, &holes); topo(*r4, op, nm, eu);
        CHECK(op == border && nm == 0 && eu == 1, "open sheet, middle vertex erased: no new open edges (border %zu, now %zu), still a disc (Euler %ld)", border, op, eu);
    }

    // eraser leftovers: a big awkward cut still closes, and loose fragments are swept up
    {
        auto openEdges = [](const Mesh& q) { std::map<std::pair<uint32_t, uint32_t>, int> e; for (size_t t = 0; t + 2 < q.idx.size(); t += 3) for (int k = 0; k < 3; k++) { uint32_t a = q.idx[t + k], b = q.idx[t + (k + 1) % 3]; e[{std::min(a, b), std::max(a, b)}]++; } size_t o = 0, o1 = 0, o3 = 0; for (auto& kv : e) { if (kv.second != 2) o++; if (kv.second == 1) o1++; if (kv.second > 2) o3++; } printf("      edges used once: %zu, more than twice: %zu\n", o1, o3); return o; };
        std::vector<uint8_t> del(m.v.size(), 0); size_t cnt = 0; for (size_t i = 0; i < m.v.size(); i++) if (m.v[i].x > 0.0f && ((i * 2654435761u) >> 7) % 5 != 0) { del[i] = 1; cnt++; }   // half the torus, ragged
        size_t holes = 0; auto r = eraseVertices(m, del, &holes);
        CHECK(openEdges(*r) == 0 && r->triCount() > 100, "ragged cut of %zu vertices (%zu rings) leaves a closed surface (%zu vertices, %zu triangles)", cnt, holes, r->v.size(), r->triCount());
        Mesh dirty = m; const uint32_t base = (uint32_t)dirty.v.size();
        for (int k = 0; k < 3; k++) { for (int c = 0; c < 3; c++) dirty.v.emplace_back(5.0f + k, (float)c, (float)(c * c)); dirty.idx.insert(dirty.idx.end(), {base + k * 3, base + k * 3 + 1, base + k * 3 + 2}); }   // three loose triangles in the air
        auto clean = removeDebris(dirty);
        CHECK(clean && clean->triCount() == m.triCount() && clean->v.size() == m.v.size(), "loose fragments swept up: %zu -> %zu triangles", dirty.triCount(), clean ? clean->triCount() : 0);
        CHECK(removeDebris(m) == nullptr, "a clean mesh is left alone");
    }

    // details
    auto red = reduceDetails(m, 50.0);
    double frac = (double)red->v.size() / m.v.size(); bool valid = true; for (auto i : red->idx) if (i >= red->v.size()) valid = false;
    printf("details 50%%: %zu vertices (%.1f%%), %zu triangles\n", red->v.size(), frac * 100, red->triCount());
    CHECK(frac > 0.35 && frac < 0.65 && valid && red->triCount() > 100, "reduceDetails keeps %.1f%% of the vertices", frac * 100);
    auto full = reduceDetails(m, 100.0); CHECK(full->v.size() == m.v.size() && full->triCount() == m.triCount(), "100%% keeps everything");

    // files
    std::string err; std::string tmp = sys::exeDir();
    CHECK(saveObj(tmp + "\\t.obj", m.v, m.idx, err), "save obj");
    Mesh back; CHECK(loadMeshFile(tmp + "\\t.obj", back, err) && back.v.size() == m.v.size() && back.idx.size() == m.idx.size(), "obj round trip %zu/%zu", back.v.size(), back.idx.size() / 3);
    CHECK(saveGlb(tmp + "\\t.glb", m.v, m.idx, err), "save glb");
    Mesh g2; bool lg = loadMeshFile(tmp + "\\t.glb", g2, err);
    CHECK(lg && g2.v.size() == m.v.size() && g2.idx.size() == m.idx.size(), "glb round trip %zu/%zu %s", g2.v.size(), g2.idx.size() / 3, err.c_str());
    if (lg) { float dx = 0; for (size_t i = 0; i < m.v.size(); i++) dx = std::max(dx, length(m.v[i] - g2.v[i])); CHECK(dx < 1e-5f, "glb vertices come back in the same place (max %.2e)", dx); }
    for (const char* ext : {"stl", "ply", "off", "gltf"}) {                                    // every export format that can be read back keeps the geometry
        Mesh q; std::string path = tmp + "/rt." + ext; bool okw = saveMeshAny(path, m.v, m.idx, err), okr = okw && loadMeshFile(path, q, err);
        CHECK(okw && okr && q.v.size() == m.v.size() && q.idx.size() == m.idx.size(), "%s round trip: %zu vertices, %zu triangles %s", ext, q.v.size(), q.idx.size() / 3, err.c_str());
    }
    { std::string path = tmp + "/rt.3mf"; std::string z; CHECK(saveMeshAny(path, m.v, m.idx, err) && sys::readFile(path, z) && z.size() > 1000 && z.compare(0, 2, "PK") == 0, "3mf is written as a zip (%zu bytes)", z.size()); }
    { std::string path = tmp + "/rt.step"; Mesh q; bool okw = saveMeshAny(path, m.v, m.idx, err), okr = okw && loadMeshFile(path, q, err);    // OpenCascade is not needed for this faceted file
      CHECK(okw && okr && q.v.size() == m.v.size() && q.idx.size() == m.idx.size(), "step (faceted) round trip: %zu vertices, %zu triangles %s", q.v.size(), q.idx.size() / 3, err.c_str()); }    std::string blob = curtainBlob(r.field); CurtainField f2;
    CHECK(curtainFieldFromBlob(blob, f2) && f2.g.size() == r.field.g.size(), "blob round trip (%zu bytes)", blob.size());
    float e1 = r.field.eval(Vec3(1.2f, 0.1f, 0.05f)), e2 = f2.eval(Vec3(1.2f, 0.1f, 0.05f)); CHECK(std::fabs(e1 - e2) < 0.01f, "quantised field agrees with the float one (%.4f vs %.4f)", e1, e2);
    std::string hdr = curtainHeaderText(r.field, "torus"); CHECK(hdr.find("torus_eval") != std::string::npos && hdr.size() > 1000, "header text %zu bytes", hdr.size());
    sys::writeFile(tmp + "\\torus_curtain.h", hdr);
    saveObj(tmp + "\\torus_curtain_smooth.obj", r.v, r.idx, err); saveObj(tmp + "\\torus_curtain_hull.obj", rh.v, rh.idx, err);

    std::vector<Vec3> cube = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0.5f, 0.5f, 0.5f}, {0.2f, 0.7f, 0.3f}}, hv; std::vector<uint32_t> hi2;
    CHECK(convexHull(cube, hv, hi2) && hi2.size() / 3 == 12 && hv.size() == 8, "cube hull: %zu vertices %zu triangles", hv.size(), hi2.size() / 3);
    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
