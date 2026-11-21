#include "project.h"
#include <cstring>
#include "sys.h"

// layout: "D3CU", u32 version (2), 9 floats transform (pos, rot, scale), i32 mode, f32 distance, i32 precision, f32 alpha, [v2: i32 density],
//         u32 vertex count, u32 index count, vertices, indices, u64 field blob size, field blob ("CVF1")
bool saveProject(const ModelDoc& d, const CurtainResult* curtain, const std::string& path, std::string& err) {
    if (!d.cur.mesh) { err = "Nothing to save."; return false; }
    std::string o("D3CU"); auto put = [&](const void* p, size_t n) { o.append((const char*)p, n); };
    const uint32_t ver = 2; put(&ver, 4);
    const float f[9] = {d.cur.xf.pos.x, d.cur.xf.pos.y, d.cur.xf.pos.z, d.cur.xf.rotDeg.x, d.cur.xf.rotDeg.y, d.cur.xf.rotDeg.z, d.cur.xf.scale.x, d.cur.xf.scale.y, d.cur.xf.scale.z}; put(f, 36);
    const int32_t mode = (int32_t)d.cur.cp.mode, prec = d.cur.cp.precision, dens = d.cur.cp.density; const float dist = d.cur.cp.distance;
    put(&mode, 4); put(&dist, 4); put(&prec, 4); put(&d.alpha, 4); put(&dens, 4);
    const uint32_t nv = (uint32_t)d.cur.mesh->v.size(), ni = (uint32_t)d.cur.mesh->idx.size(); put(&nv, 4); put(&ni, 4);
    put(d.cur.mesh->v.data(), nv * 12ull); put(d.cur.mesh->idx.data(), ni * 4ull);
    const std::string blob = curtain ? curtainBlob(curtain->field) : std::string(); const uint64_t bs = blob.size(); put(&bs, 8); o += blob;
    if (!sys::writeFile(path, o)) { err = "Cannot write " + path; return false; }
    return true;
}

bool loadProject(const std::string& path, ModelDoc& d, std::string& err) {
    std::string s; if (!sys::readFile(path, s) || s.size() < 80 || memcmp(s.data(), "D3CU", 4)) { err = "This is not a Doppler's 3D Curtain project."; return false; }
    uint32_t ver; memcpy(&ver, s.data() + 4, 4); size_t p = 8;
    auto get = [&](void* q, size_t n) { if (p + n > s.size()) return false; memcpy(q, s.data() + p, n); p += n; return true; };
    float f[9]; int32_t mode, prec, dens = 30000; float dist, alpha; uint32_t nv, ni;
    if (!get(f, 36) || !get(&mode, 4) || !get(&dist, 4) || !get(&prec, 4) || !get(&alpha, 4) || (ver >= 2 && !get(&dens, 4)) || !get(&nv, 4) || !get(&ni, 4) || (uint64_t)nv * 12 + (uint64_t)ni * 4 > s.size()) { err = "The project file is damaged."; return false; }
    auto m = std::make_shared<Mesh>(); m->v.resize(nv); m->idx.resize(ni);
    if (!get(m->v.data(), nv * 12ull) || !get(m->idx.data(), ni * 4ull)) { err = "The project file is cut off."; return false; }
    for (auto i : m->idx) if (i >= nv) { err = "The project file is damaged (bad triangle)."; return false; }
    d.cur.mesh = m; Vec3 lo, hi; m->bounds(lo, hi); d.pivot = (lo + hi) * 0.5f;
    d.cur.xf.pos = Vec3(f[0], f[1], f[2]); d.cur.xf.rotDeg = Vec3(f[3], f[4], f[5]); d.cur.xf.scale = Vec3(f[6], f[7], f[8]);
    d.cur.cp.mode = mode == 1 ? CurtainMode::Hull : CurtainMode::Smooth; d.cur.cp.distance = dist; d.cur.cp.precision = prec; d.cur.cp.density = dens; d.alpha = alpha;
    d.rebuild(); d.fitCamera(); return true;
}
