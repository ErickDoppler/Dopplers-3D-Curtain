#include "mesh.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include "json.h"
#include "sys.h"

void Mesh::bounds(Vec3& lo, Vec3& hi) const {
    if (v.empty()) { lo = hi = Vec3(); return; }
    lo = hi = v[0];
    for (auto& p : v) { lo = vmin(lo, p); hi = vmax(hi, p); }
}

Mat4 xformMatrix(const Xform& x, const Vec3& pivot) {
    Mat4 R = Mat4::rotZ(rad(x.rotDeg.z)) * Mat4::rotY(rad(x.rotDeg.y)) * Mat4::rotX(rad(x.rotDeg.x));
    return Mat4::translate(x.pos) * R * Mat4::scale(x.scale) * Mat4::translate(pivot * -1.0f);
}
void transformMesh(const Mesh& m, const Mat4& M, std::vector<Vec3>& out) {
    out.resize(m.v.size());
    for (size_t i = 0; i < m.v.size(); i++) out[i] = M.point(m.v[i]);
}

void computeNormals(const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::vector<Vec3>& n) {
    n.assign(v.size(), Vec3());
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const Vec3 &a = v[idx[t]], &b = v[idx[t + 1]], &c = v[idx[t + 2]];
        Vec3 f = cross(b - a, c - a);                       // area weighted
        n[idx[t]] += f; n[idx[t + 1]] += f; n[idx[t + 2]] += f;
    }
    for (auto& x : n) x = normalized(x);
}

namespace {
bool fail(std::string& err, const std::string& m) { err = m; return false; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string extOf(const std::string& p) { size_t k = p.find_last_of('.'); return k == std::string::npos ? "" : lower(p.substr(k + 1)); }

// ------------------------------------------------------------------ OBJ
bool loadObj(const std::string& text, Mesh& m, std::string& err) {
    const char* p = text.c_str(); const char* end = p + text.size();
    std::vector<uint32_t> poly;
    while (p < end) {
        const char* eol = (const char*)memchr(p, '\n', end - p); if (!eol) eol = end;
        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            char* q; float x = strtof(p + 1, &q), y = strtof(q, &q), z = strtof(q, &q); m.v.emplace_back(x, y, z);
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            poly.clear(); const char* q = p + 1;
            while (q < eol) {
                while (q < eol && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
                if (q >= eol) break;
                char* e; long i = strtol(q, &e, 10);
                if (e == q) break;
                long k = i < 0 ? (long)m.v.size() + i : i - 1;
                if (k < 0 || k >= (long)m.v.size()) return fail(err, "OBJ: a face uses a vertex that does not exist");
                poly.push_back((uint32_t)k);
                q = e; while (q < eol && *q != ' ' && *q != '\t' && *q != '\r') q++;       // skip /vt/vn
            }
            for (size_t i = 2; i < poly.size(); i++) { m.idx.push_back(poly[0]); m.idx.push_back(poly[i - 1]); m.idx.push_back(poly[i]); }
        }
        p = eol + 1;
    }
    return true;
}

// ------------------------------------------------------------------ STL (binary or ASCII), vertices welded
struct Key { uint32_t a, b, c; bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KeyHash { size_t operator()(const Key& k) const { return (size_t)(k.a * 73856093u ^ k.b * 19349663u ^ k.c * 83492791u); } };
struct Welder {
    Mesh& m; std::unordered_map<Key, uint32_t, KeyHash> map;
    explicit Welder(Mesh& mm) : m(mm) {}
    uint32_t add(const Vec3& p) {
        Key k; memcpy(&k.a, &p.x, 4); memcpy(&k.b, &p.y, 4); memcpy(&k.c, &p.z, 4);
        auto it = map.find(k); if (it != map.end()) return it->second;
        uint32_t i = (uint32_t)m.v.size(); m.v.push_back(p); map.emplace(k, i); return i;
    }
};
bool loadStl(const std::string& d, Mesh& m, std::string& err) {
    Welder w(m);
    if (d.size() >= 84) {
        uint32_t n; memcpy(&n, d.data() + 80, 4);
        if (84 + (size_t)n * 50 == d.size()) {
            for (uint32_t t = 0; t < n; t++) {
                const char* r = d.data() + 84 + (size_t)t * 50 + 12; float f[9]; memcpy(f, r, 36);
                for (int k = 0; k < 3; k++) m.idx.push_back(w.add(Vec3(f[k * 3], f[k * 3 + 1], f[k * 3 + 2])));
            }
            return true;
        }
    }
    const char* p = d.c_str(); const char* end = p + d.size(); int c = 0;
    while (p < end) {
        const char* v = strstr(p, "vertex"); if (!v) break;
        char* q; float x = strtof(v + 6, &q), y = strtof(q, &q), z = strtof(q, &q);
        m.idx.push_back(w.add(Vec3(x, y, z))); c++; p = q;
    }
    if (c == 0 || c % 3) return fail(err, "STL: no triangles found (is the file damaged?)");
    return true;
}

// ------------------------------------------------------------------ PLY
bool loadPly(const std::string& d, Mesh& m, std::string& err) {
    size_t he = d.find("end_header"); if (he == std::string::npos) return fail(err, "PLY: no header");
    size_t body = d.find('\n', he); if (body == std::string::npos) return fail(err, "PLY: no body"); body++;
    struct Prop { std::string name, type, countType; bool list = false; };
    struct Elem { std::string name; size_t count = 0; std::vector<Prop> props; };
    std::vector<Elem> el; std::string fmt;
    { size_t pos = 0; while (pos < he) {
        size_t e = d.find('\n', pos); if (e == std::string::npos || e > he) e = he;
        std::string line = d.substr(pos, e - pos); pos = e + 1;
        char a[64], b[64], c[64], dd[64];
        if (sscanf(line.c_str(), "format %63s", a) == 1) fmt = a;
        else if (sscanf(line.c_str(), "element %63s %63s", a, b) == 2) { Elem x; x.name = a; x.count = (size_t)atoll(b); el.push_back(x); }
        else if (sscanf(line.c_str(), "property list %63s %63s %63s", a, b, c) == 3 && !el.empty()) { Prop p; p.list = true; p.countType = a; p.type = b; p.name = c; el.back().props.push_back(p); }
        else if (sscanf(line.c_str(), "property %63s %63s", a, dd) == 2 && !el.empty()) { Prop p; p.type = a; p.name = dd; el.back().props.push_back(p); }
    } }
    auto sz = [](const std::string& t) { if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1; if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2; if (t == "double" || t == "float64") return 8; return 4; };
    auto rd = [&](const char* q, const std::string& t) -> double {
        if (t == "char" || t == "int8") return (double)*(const int8_t*)q;
        if (t == "uchar" || t == "uint8") return (double)*(const uint8_t*)q;
        if (t == "short" || t == "int16") { int16_t x; memcpy(&x, q, 2); return x; }
        if (t == "ushort" || t == "uint16") { uint16_t x; memcpy(&x, q, 2); return x; }
        if (t == "int" || t == "int32") { int32_t x; memcpy(&x, q, 4); return x; }
        if (t == "uint" || t == "uint32") { uint32_t x; memcpy(&x, q, 4); return x; }
        if (t == "double" || t == "float64") { double x; memcpy(&x, q, 8); return x; }
        float x; memcpy(&x, q, 4); return x; };
    const bool ascii = fmt == "ascii";
    if (!ascii && fmt != "binary_little_endian") return fail(err, "PLY: big-endian files are not supported");
    const char* p = d.data() + body; const char* end = d.data() + d.size();
    for (auto& e : el) {
        for (size_t i = 0; i < e.count; i++) {
            Vec3 pos; std::vector<uint32_t> poly;
            for (auto& pr : e.props) {
                if (pr.list) {
                    int n; std::vector<double> vals;
                    if (ascii) { char* q; n = (int)strtol(p, &q, 10); p = q; for (int k = 0; k < n; k++) { vals.push_back(strtod(p, &q)); p = q; } }
                    else {
                        if (p + sz(pr.countType) > end) return fail(err, "PLY: file is cut off");
                        n = (int)rd(p, pr.countType); p += sz(pr.countType); int s = sz(pr.type);
                        if (p + (size_t)s * n > end) return fail(err, "PLY: file is cut off");
                        for (int k = 0; k < n; k++) { vals.push_back(rd(p, pr.type)); p += s; }
                    }
                    if (e.name == "face" && (pr.name == "vertex_indices" || pr.name == "vertex_index")) for (double v : vals) poly.push_back((uint32_t)v);
                } else {
                    double v;
                    if (ascii) { char* q; v = strtod(p, &q); p = q; }
                    else { if (p + sz(pr.type) > end) return fail(err, "PLY: file is cut off"); v = rd(p, pr.type); p += sz(pr.type); }
                    if (e.name == "vertex") { if (pr.name == "x") pos.x = (float)v; else if (pr.name == "y") pos.y = (float)v; else if (pr.name == "z") pos.z = (float)v; }
                }
            }
            if (e.name == "vertex") m.v.push_back(pos);
            else if (e.name == "face") for (size_t k = 2; k < poly.size(); k++) { m.idx.push_back(poly[0]); m.idx.push_back(poly[k - 1]); m.idx.push_back(poly[k]); }
        }
    }
    for (auto i : m.idx) if (i >= m.v.size()) return fail(err, "PLY: a face uses a vertex that does not exist");
    return true;
}

// ------------------------------------------------------------------ glTF / GLB
std::string b64decode(const std::string& s) {
    static int T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; i++) T[i] = -1; const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; i < 64; i++) T[(unsigned char)a[i]] = i; init = true; }
    std::string o; int v = 0, b = -8;
    for (unsigned char c : s) { if (T[c] < 0) continue; v = (v << 6) + T[c]; b += 6; if (b >= 0) { o.push_back((char)((v >> b) & 0xFF)); b -= 8; } }
    return o;
}
Mat4 quatMat(float x, float y, float z, float w) {
    Mat4 r; float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz); r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz); r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy); r.m[9] = 2 * (yz - wx); r.m[10] = 1 - 2 * (xx + yy);
    return r;
}
struct Gltf {
    Json j; std::vector<std::string> bufs; std::string dir; Mesh* out = nullptr; std::string err;
    bool accessor(int ai, int comps, std::vector<double>& vals) {
        const Json& a = j.get("accessors").a.at(ai); const Json& bv = j.get("bufferViews").a.at(a.integer("bufferView", 0));
        const std::string& buf = bufs.at(bv.integer("buffer", 0));
        const int ct = a.integer("componentType", 5126); const size_t cnt = (size_t)a.integer("count", 0);
        const int cs = (ct == 5126 || ct == 5125) ? 4 : (ct == 5123 || ct == 5122) ? 2 : 1;
        const size_t stride = bv.integer("byteStride", 0) ? (size_t)bv.integer("byteStride", 0) : (size_t)cs * comps;
        const size_t off = (size_t)bv.integer("byteOffset", 0) + (size_t)a.integer("byteOffset", 0);
        if (off + (cnt ? (cnt - 1) * stride + cs * comps : 0) > buf.size()) { err = "glTF: an accessor reaches past its buffer"; return false; }
        vals.resize(cnt * comps);
        for (size_t i = 0; i < cnt; i++) for (int c = 0; c < comps; c++) {
            const char* q = buf.data() + off + i * stride + c * cs; double v;
            if (ct == 5126) { float f; memcpy(&f, q, 4); v = f; }
            else if (ct == 5125) { uint32_t u; memcpy(&u, q, 4); v = u; }
            else if (ct == 5123) { uint16_t u; memcpy(&u, q, 2); v = u; }
            else if (ct == 5122) { int16_t u; memcpy(&u, q, 2); v = u; }
            else v = *(const uint8_t*)q;
            vals[i * comps + c] = v;
        }
        return true;
    }
    bool mesh(int mi, const Mat4& M) {
        for (const Json& pr : j.get("meshes").a.at(mi).get("primitives").a) {
            if (pr.integer("mode", 4) != 4) continue;                                        // triangles only
            std::vector<double> pos; if (!accessor(pr.get("attributes").integer("POSITION", -1), 3, pos)) return false;
            const uint32_t base = (uint32_t)out->v.size();
            for (size_t i = 0; i + 2 < pos.size(); i += 3) { Vec3 p = M.point(Vec3((float)pos[i], (float)pos[i + 1], (float)pos[i + 2])); out->v.emplace_back(p.x, -p.z, p.y); }   // Y-up -> Z-up
            std::vector<double> id;
            if (pr.has("indices")) { if (!accessor(pr.integer("indices", 0), 1, id)) return false; for (double d : id) out->idx.push_back(base + (uint32_t)d); }
            else for (uint32_t i = 0; i + 2 < pos.size() / 3; i++) out->idx.push_back(base + i);
        }
        return true;
    }
    bool node(int ni, const Mat4& parent, int depth) {
        if (depth > 64) return true;
        const Json& n = j.get("nodes").a.at(ni); Mat4 L;
        if (n.has("matrix") && n.get("matrix").a.size() == 16) { for (int i = 0; i < 16; i++) L.m[i] = (float)n.get("matrix").a[i].num(); }
        else {
            Vec3 t, s(1, 1, 1); Mat4 R;
            if (n.has("translation")) { auto& a = n.get("translation").a; t = Vec3((float)a[0].num(), (float)a[1].num(), (float)a[2].num()); }
            if (n.has("scale")) { auto& a = n.get("scale").a; s = Vec3((float)a[0].num(), (float)a[1].num(), (float)a[2].num()); }
            if (n.has("rotation")) { auto& a = n.get("rotation").a; R = quatMat((float)a[0].num(), (float)a[1].num(), (float)a[2].num(), (float)a[3].num()); }
            L = Mat4::translate(t) * R * Mat4::scale(s);
        }
        Mat4 M = parent * L;
        if (n.has("mesh") && !mesh(n.integer("mesh", 0), M)) return false;
        for (const Json& c : n.get("children").a) if (!node(c.integer(), M, depth + 1)) return false;
        return true;
    }
};
bool loadGltf(const std::string& path, const std::string& d, bool glb, Mesh& m, std::string& err) {
    Gltf g; g.out = &m; std::string jsonText, bin;
    g.dir = path.substr(0, path.find_last_of("\\/") + 1);
    if (glb) {
        if (d.size() < 20) return fail(err, "GLB: file too small");
        size_t pos = 12;
        while (pos + 8 <= d.size()) {
            uint32_t len, type; memcpy(&len, d.data() + pos, 4); memcpy(&type, d.data() + pos + 4, 4); pos += 8;
            if (pos + len > d.size()) return fail(err, "GLB: chunk reaches past the file");
            if (type == 0x4E4F534A) jsonText.assign(d.data() + pos, len); else if (type == 0x004E4942 && bin.empty()) bin.assign(d.data() + pos, len);
            pos += (len + 3) & ~3u;
        }
    } else jsonText = d;
    bool ok = true; g.j = Json::parse(jsonText, &ok); if (!ok) return fail(err, "glTF: the JSON is damaged");
    for (const Json& b : g.j.get("buffers").a) {
        std::string uri = b.str("uri");
        if (uri.empty()) g.bufs.push_back(bin);
        else if (uri.rfind("data:", 0) == 0) { size_t c = uri.find(','); g.bufs.push_back(b64decode(c == std::string::npos ? "" : uri.substr(c + 1))); }
        else {
            std::string data; std::string f = g.dir + uri; for (auto& ch : f) if (ch == '/') ch = '\\';
            if (!sys::readFile(f, data)) return fail(err, "glTF: cannot read the buffer file " + uri);
            g.bufs.push_back(data);
        }
    }
    try {
        const Json& scenes = g.j.get("scenes");
        if (scenes.a.empty()) { for (size_t i = 0; i < g.j.get("meshes").a.size(); i++) if (!g.mesh((int)i, Mat4())) return fail(err, g.err); }
        else { const Json& sc = scenes.a.at(g.j.integer("scene", 0)); for (const Json& n : sc.get("nodes").a) if (!g.node(n.integer(), Mat4(), 0)) return fail(err, g.err); }
    } catch (...) { return fail(err, "glTF: the file refers to something that is not there"); }
    return true;
}
}  // namespace

bool loadMeshFile(const std::string& path, Mesh& out, std::string& err) {
    const std::string e = extOf(path); std::string d;
    if (e == "step" || e == "stp") return fail(err, "STEP / STP files need a CAD kernel to turn their curved surfaces into triangles. That reader is not built yet; export the part as STL or OBJ from your CAD program for now.");
    if (!sys::readFile(path, d)) return fail(err, "Cannot read " + path);
    out = Mesh(); bool ok;
    if (e == "obj") ok = loadObj(d, out, err); else if (e == "stl") ok = loadStl(d, out, err); else if (e == "ply") ok = loadPly(d, out, err);
    else if (e == "glb") ok = loadGltf(path, d, true, out, err); else if (e == "gltf") ok = loadGltf(path, d, false, out, err);
    else return fail(err, "Unknown file type ." + e);
    if (!ok) return false;
    if (out.v.empty() || out.idx.empty()) return fail(err, "The file holds no triangles.");
    // formats like glTF and PLY often repeat a vertex for every triangle corner (split normals, UVs): merge identical positions so that neighbours share vertices
    { Mesh w; w.idx.reserve(out.idx.size()); std::vector<uint32_t> map(out.v.size()); Welder wl(w);
      for (size_t i = 0; i < out.v.size(); i++) { Vec3 p = out.v[i]; if (p.x == 0) p.x = 0.0f; if (p.y == 0) p.y = 0.0f; if (p.z == 0) p.z = 0.0f; map[i] = wl.add(p); }
      for (size_t t = 0; t + 2 < out.idx.size(); t += 3) { uint32_t a = map[out.idx[t]], b = map[out.idx[t + 1]], c = map[out.idx[t + 2]]; if (a != b && b != c && a != c) { w.idx.push_back(a); w.idx.push_back(b); w.idx.push_back(c); } }
      if (w.idx.empty()) return fail(err, "The file holds no usable triangles."); out = std::move(w); }
    return true;
}

bool saveObj(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    std::string s; s.reserve(v.size() * 30 + idx.size() * 10); char b[128];
    s += "# Dopplers 3D Curtain\n";
    for (auto& p : v) { int n = snprintf(b, sizeof b, "v %.6g %.6g %.6g\n", p.x, p.y, p.z); s.append(b, n); }
    for (size_t i = 0; i + 2 < idx.size(); i += 3) { int n = snprintf(b, sizeof b, "f %u %u %u\n", idx[i] + 1, idx[i + 1] + 1, idx[i + 2] + 1); s.append(b, n); }
    if (!sys::writeFile(path, s)) return fail(err, "Cannot write " + path);
    return true;
}
bool saveGlb(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    std::vector<Vec3> n; computeNormals(v, idx, n);
    std::string bin; Vec3 lo = v.empty() ? Vec3() : Vec3(v[0].x, v[0].z, -v[0].y), hi = lo;
    for (size_t i = 0; i < v.size(); i++) { float f[3] = {v[i].x, v[i].z, -v[i].y}; bin.append((const char*)f, 12); lo = vmin(lo, Vec3(f[0], f[1], f[2])); hi = vmax(hi, Vec3(f[0], f[1], f[2])); }
    const size_t nOff = bin.size();
    for (size_t i = 0; i < n.size(); i++) { float f[3] = {n[i].x, n[i].z, -n[i].y}; bin.append((const char*)f, 12); }
    const size_t iOff = bin.size(); bin.append((const char*)idx.data(), idx.size() * 4);
    while (bin.size() % 4) bin.push_back(0);
    char js[1600];
    int jl = snprintf(js, sizeof js,
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Dopplers 3D Curtain\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2,\"mode\":4}]}],"
        "\"buffers\":[{\"byteLength\":%zu}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu,\"target\":34962},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34963}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},{\"bufferView\":1,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}]}",
        bin.size(), nOff, nOff, nOff, iOff, idx.size() * 4, v.size(), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, v.size(), idx.size());
    std::string jsn(js, jl); while (jsn.size() % 4) jsn.push_back(' ');
    uint32_t total = 12 + 8 + (uint32_t)jsn.size() + 8 + (uint32_t)bin.size(), magic = 0x46546C67, ver = 2, jlen = (uint32_t)jsn.size(), jt = 0x4E4F534A, blen = (uint32_t)bin.size(), bt = 0x004E4942;
    std::string o; o.append((char*)&magic, 4); o.append((char*)&ver, 4); o.append((char*)&total, 4); o.append((char*)&jlen, 4); o.append((char*)&jt, 4); o += jsn;
    o.append((char*)&blen, 4); o.append((char*)&bt, 4); o += bin;
    if (!sys::writeFile(path, o)) return fail(err, "Cannot write " + path);
    return true;
}

// ------------------------------------------------------------------ object details
std::shared_ptr<Mesh> reduceDetails(const Mesh& m, double q) {
    auto out = std::make_shared<Mesh>();
    const size_t n = m.v.size(); if (n == 0) return out;
    Vec3 lo, hi; m.bounds(lo, hi);
    const double ext = std::max({(double)(hi.x - lo.x), (double)(hi.y - lo.y), (double)(hi.z - lo.z), 1e-12}), cell = ext / 10000.0, keepP = std::min(1.0, std::max(0.0, q / 100.0));
    std::vector<uint8_t> kept(n, 0); size_t keptN = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t cx = (uint64_t)std::min(9999.0, std::floor((m.v[i].x - lo.x) / cell)), cy = (uint64_t)std::min(9999.0, std::floor((m.v[i].y - lo.y) / cell)), cz = (uint64_t)std::min(9999.0, std::floor((m.v[i].z - lo.z) / cell));
        uint64_t h = (cx * 10000 + cy) * 10000 + cz; h += 0x9E3779B97F4A7C15ull; h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull; h = (h ^ (h >> 27)) * 0x94D049BB133111EBull; h ^= h >> 31;
        const double u = (double)(h >> 11) / 9007199254740992.0;
        if (u < keepP) { kept[i] = 1; keptN++; }
    }
    if (keptN < 4 && n >= 4) { for (size_t i = 0; i < n && keptN < 4; i++) if (!kept[i]) { kept[i] = 1; keptN++; } }
    // adjacency, then every removed vertex joins the nearest kept vertex along the surface (this closes the holes: the triangles around it collapse onto the kept ones)
    std::vector<uint32_t> start(n + 1, 0);
    for (size_t t = 0; t < m.idx.size(); t++) start[m.idx[t] + 1] += 2;
    for (size_t i = 0; i < n; i++) start[i + 1] += start[i];
    std::vector<uint32_t> adj(start[n]), fill(start.begin(), start.end() - 1);
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) for (int k = 0; k < 3; k++) { uint32_t a = m.idx[t + k], b = m.idx[t + (k + 1) % 3]; adj[fill[a]++] = b; adj[fill[b]++] = a; }
    std::vector<uint32_t> to(n, 0xFFFFFFFFu), queue; queue.reserve(n);
    for (size_t i = 0; i < n; i++) if (kept[i]) { to[i] = (uint32_t)i; queue.push_back((uint32_t)i); }
    for (size_t h = 0; h < queue.size(); h++) { uint32_t c = queue[h]; for (uint32_t k = start[c]; k < start[c + 1]; k++) { uint32_t nb = adj[k]; if (to[nb] == 0xFFFFFFFFu) { to[nb] = to[c]; queue.push_back(nb); } } }
    for (size_t i = 0; i < n; i++) if (to[i] == 0xFFFFFFFFu) to[i] = (uint32_t)i;                  // islands without a kept vertex stay as they are
    std::vector<uint32_t> remap(n, 0xFFFFFFFFu); std::set<std::array<uint32_t, 3>> seen;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        uint32_t a = to[m.idx[t]], b = to[m.idx[t + 1]], c = to[m.idx[t + 2]];
        if (a == b || b == c || a == c) continue;
        std::array<uint32_t, 3> key = {a, b, c}; std::sort(key.begin(), key.end()); if (!seen.insert(key).second) continue;
        for (uint32_t x : {a, b, c}) { if (remap[x] == 0xFFFFFFFFu) { remap[x] = (uint32_t)out->v.size(); out->v.push_back(m.v[x]); } out->idx.push_back(remap[x]); }
    }
    return out;
}

// ------------------------------------------------------------------ eraser
namespace {
// ear clipping of one ring (vertex ids in the direction the new triangles must run); the ring is flattened onto its best-fit plane
void triangulateRing(const std::vector<Vec3>& P, const std::vector<uint32_t>& L, std::vector<uint32_t>& out) {
    const size_t n = L.size(); if (n < 3) return;
    if (n == 3) { out.insert(out.end(), {L[0], L[1], L[2]}); return; }
    Vec3 N;                                                                         // Newell normal
    for (size_t i = 0; i < n; i++) { const Vec3 &a = P[L[i]], &b = P[L[(i + 1) % n]]; N.x += (a.y - b.y) * (a.z + b.z); N.y += (a.z - b.z) * (a.x + b.x); N.z += (a.x - b.x) * (a.y + b.y); }
    N = normalized(N);
    Vec3 u = std::fabs(N.x) < 0.9f ? cross(N, Vec3(1, 0, 0)) : cross(N, Vec3(0, 1, 0)); u = normalized(u); const Vec3 w = cross(N, u);
    std::vector<float> X(n), Y(n); for (size_t i = 0; i < n; i++) { X[i] = dot(P[L[i]], u); Y[i] = dot(P[L[i]], w); }
    auto cr = [&](size_t a, size_t b, size_t c) { return (X[b] - X[a]) * (Y[c] - Y[b]) - (Y[b] - Y[a]) * (X[c] - X[b]); };
    auto inside = [&](size_t p, size_t a, size_t b, size_t c) { const float d1 = cr(a, b, p) , d2 = (X[c] - X[b]) * (Y[p] - Y[b]) - (Y[c] - Y[b]) * (X[p] - X[b]), d3 = (X[a] - X[c]) * (Y[p] - Y[c]) - (Y[a] - Y[c]) * (X[p] - X[c]); (void)d1;
        const float e1 = (X[b] - X[a]) * (Y[p] - Y[a]) - (Y[b] - Y[a]) * (X[p] - X[a]); return e1 > 0 && d2 > 0 && d3 > 0; };
    auto d2 = [&](size_t a, size_t b) { const Vec3 d = P[L[a]] - P[L[b]]; return dot(d, d); };
    std::vector<size_t> r(n); for (size_t i = 0; i < n; i++) r[i] = i;
    while (r.size() > 3) {
        const size_t m = r.size(); int best = -1; float bestScore = 1e30f; int fallback = -1; float fbCross = -1e30f;
        for (size_t i = 0; i < m; i++) {
            const size_t a = r[(i + m - 1) % m], b = r[i], c = r[(i + 1) % m]; const float cc = cr(a, b, c);
            if (cc > fbCross) { fbCross = cc; fallback = (int)i; }
            if (cc <= 0.0f) continue;                                             // reflex or flat corner
            bool ok = true; for (size_t j = 0; j < m && ok; j++) { const size_t p = r[j]; if (p != a && p != b && p != c && inside(p, a, b, c)) ok = false; }
            if (!ok) continue;
            const float score = std::max({d2(a, b), d2(b, c), d2(c, a)});            // prefer short ribs
            if (score < bestScore) { bestScore = score; best = (int)i; }
        }
        const int i = best >= 0 ? best : fallback;
        out.insert(out.end(), {L[r[(i + m - 1) % m]], L[r[i]], L[r[(i + 1) % m]]}); r.erase(r.begin() + i);
    }
    out.insert(out.end(), {L[r[0]], L[r[1]], L[r[2]]});
}
}  // namespace

std::shared_ptr<Mesh> eraseVertices(const Mesh& m, const std::vector<uint8_t>& del, size_t* holesClosed) {
    auto res = std::make_shared<Mesh>(); const size_t T = m.idx.size() / 3;
    std::vector<uint32_t> tris; tris.reserve(m.idx.size()); std::vector<std::pair<uint32_t, uint32_t>> cut;   // edges around the holes, in the direction the closing triangles need
    for (size_t t = 0; t < T; t++) {
        const uint32_t a = m.idx[t * 3], b = m.idx[t * 3 + 1], c = m.idx[t * 3 + 2]; const int da = del[a], db = del[b], dc = del[c], k = da + db + dc;
        if (k == 0) tris.insert(tris.end(), {a, b, c});
        else if (k == 1) { if (da) cut.push_back({b, c}); else if (db) cut.push_back({c, a}); else cut.push_back({a, b}); }   // the edge opposite the removed vertex stays in the neighbour
    }
    // chain the cut edges into rings; a vertex that appears twice splits its ring into two
    std::unordered_map<uint32_t, std::vector<int>> from; for (size_t i = 0; i < cut.size(); i++) from[cut[i].first].push_back((int)i);
    std::vector<char> used(cut.size(), 0); size_t closed = 0; std::vector<uint32_t> fill;
    for (size_t e0 = 0; e0 < cut.size(); e0++) {
        if (used[e0]) continue;
        std::vector<uint32_t> ring{cut[e0].first}; std::unordered_map<uint32_t, size_t> pos{{cut[e0].first, 0}}; used[e0] = 1; uint32_t v = cut[e0].second; bool open = false;
        while (true) {
            auto it = pos.find(v);
            if (it != pos.end()) {                                                   // the ring came back to a vertex it already has
                std::vector<uint32_t> sub(ring.begin() + it->second, ring.end());
                triangulateRing(m.v, sub, fill); closed++;
                for (size_t q = it->second + 1; q < ring.size(); q++) pos.erase(ring[q]);
                ring.resize(it->second + 1);
                if (ring.size() == 1 && it->second == 0) break;                      // the first vertex is reached: done
            } else { pos[v] = ring.size(); ring.push_back(v); }
            int nxt = -1; for (int ei : from[v]) if (!used[ei]) { nxt = ei; break; }
            if (nxt < 0) { open = true; break; }                                     // an open chain (the hole runs into the border of the mesh): left alone
            used[nxt] = 1; v = cut[nxt].second;
        }
        (void)open;
    }
    tris.insert(tris.end(), fill.begin(), fill.end());
    std::vector<uint32_t> remap(m.v.size(), 0xFFFFFFFFu);
    for (uint32_t i : tris) { if (remap[i] == 0xFFFFFFFFu) { remap[i] = (uint32_t)res->v.size(); res->v.push_back(m.v[i]); } res->idx.push_back(remap[i]); }
    if (holesClosed) *holesClosed = closed;
    return res;
}
