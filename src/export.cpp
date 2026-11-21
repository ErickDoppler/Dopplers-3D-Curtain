// Writers for the mesh formats: STL (binary), OBJ, PLY (binary), glTF (+ .bin), GLB, 3MF, OFF and STEP (faceted B-rep). The format comes from the file extension.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "mesh.h"
#include "sys.h"

namespace {
bool fail(std::string& err, const std::string& m) { err = m; return false; }
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string extOf(const std::string& p) { size_t k = p.find_last_of('.'); return k == std::string::npos ? "" : lower(p.substr(k + 1)); }
std::string stemOf(const std::string& p) { size_t k = p.find_last_of("\\/"); std::string b = k == std::string::npos ? p : p.substr(k + 1); size_t d = b.find_last_of('.'); return d == std::string::npos ? b : b.substr(0, d); }
std::string dirOf(const std::string& p) { size_t k = p.find_last_of("\\/"); return k == std::string::npos ? "" : p.substr(0, k + 1); }
bool check(const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    if (v.empty() || idx.size() < 3) return fail(err, "There is nothing to save (the object has no triangles).");
    for (uint32_t i : idx) if (i >= v.size()) return fail(err, "The mesh is damaged (a triangle uses a missing vertex).");
    return true;
}
template <class T> void put(std::string& s, const T& x) { s.append((const char*)&x, sizeof x); }

// ------------------------------------------------------------------ STL (binary)
bool saveStl(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    const uint32_t n = (uint32_t)(idx.size() / 3); std::string s(80, ' '); s.replace(0, 24, "Dopplers 3D Curtain  STL"); s.reserve(84 + (size_t)n * 50); put(s, n);
    for (uint32_t t = 0; t < n; t++) {
        const Vec3 &a = v[idx[t * 3]], &b = v[idx[t * 3 + 1]], &c = v[idx[t * 3 + 2]]; const Vec3 nn = normalized(cross(b - a, c - a));
        put(s, nn.x); put(s, nn.y); put(s, nn.z); for (const Vec3* p : {&a, &b, &c}) { put(s, p->x); put(s, p->y); put(s, p->z); } put(s, (uint16_t)0);
    }
    return sys::writeFile(path, s) || fail(err, "Cannot write " + path);
}

// ------------------------------------------------------------------ PLY (binary little endian)
bool savePly(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    char h[256]; int n = snprintf(h, sizeof h, "ply\nformat binary_little_endian 1.0\ncomment Dopplers 3D Curtain\nelement vertex %zu\nproperty float x\nproperty float y\nproperty float z\nelement face %zu\nproperty list uchar int vertex_indices\nend_header\n", v.size(), idx.size() / 3);
    std::string s(h, n); s.reserve(n + v.size() * 12 + idx.size() / 3 * 13);
    for (auto& p : v) { put(s, p.x); put(s, p.y); put(s, p.z); }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) { put(s, (uint8_t)3); put(s, (int32_t)idx[t]); put(s, (int32_t)idx[t + 1]); put(s, (int32_t)idx[t + 2]); }
    return sys::writeFile(path, s) || fail(err, "Cannot write " + path);
}

// ------------------------------------------------------------------ OFF
bool saveOff(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    std::string s = "OFF\n" + std::to_string(v.size()) + " " + std::to_string(idx.size() / 3) + " 0\n"; char b[128];
    for (auto& p : v) { int n = snprintf(b, sizeof b, "%.7g %.7g %.7g\n", p.x, p.y, p.z); s.append(b, n); }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) { int n = snprintf(b, sizeof b, "3 %u %u %u\n", idx[t], idx[t + 1], idx[t + 2]); s.append(b, n); }
    return sys::writeFile(path, s) || fail(err, "Cannot write " + path);
}

// ------------------------------------------------------------------ glTF and GLB (the format is Y-up: the object is turned from this program's Z-up)
void buildGltf(const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, const std::string& uri, std::string& json, std::string& bin) {
    std::vector<Vec3> n; computeNormals(v, idx, n); Vec3 lo = Vec3(v[0].x, v[0].z, -v[0].y), hi = lo; bin.clear();
    for (auto& p : v) { float f[3] = {p.x, p.z, -p.y}; bin.append((const char*)f, 12); lo = vmin(lo, Vec3(f[0], f[1], f[2])); hi = vmax(hi, Vec3(f[0], f[1], f[2])); }
    const size_t nOff = bin.size(); for (auto& p : n) { float f[3] = {p.x, p.z, -p.y}; bin.append((const char*)f, 12); }
    const size_t iOff = bin.size(); bin.append((const char*)idx.data(), idx.size() * 4); while (bin.size() % 4) bin.push_back(0);
    char js[2000]; std::string u = uri.empty() ? "" : ",\"uri\":\"" + uri + "\"";
    int jl = snprintf(js, sizeof js,
        "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Dopplers 3D Curtain\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2,\"mode\":4}]}],\"buffers\":[{\"byteLength\":%zu%s}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu,\"target\":34962},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34963}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]},{\"bufferView\":1,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}]}",
        bin.size(), u.c_str(), nOff, nOff, nOff, iOff, idx.size() * 4, v.size(), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, v.size(), idx.size());
    json.assign(js, jl);
}
bool saveGltf(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    std::string json, bin; const std::string binName = stemOf(path) + ".bin"; buildGltf(v, idx, binName, json, bin);
    if (!sys::writeFile(dirOf(path) + binName, bin)) return fail(err, "Cannot write " + dirOf(path) + binName);
    return sys::writeFile(path, json) || fail(err, "Cannot write " + path);
}

// ------------------------------------------------------------------ 3MF (a zip with stored entries)
uint32_t crc32(const std::string& d) {
    static uint32_t T[256]; static bool init = false; if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu; for (unsigned char ch : d) c = T[(c ^ ch) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
bool save3mf(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    std::string model = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n<metadata name=\"Application\">Dopplers 3D Curtain</metadata>\n<resources>\n<object id=\"1\" type=\"model\">\n<mesh>\n<vertices>\n";
    char b[160]; model.reserve(v.size() * 60 + idx.size() * 14);
    for (auto& p : v) { int n = snprintf(b, sizeof b, "<vertex x=\"%.7g\" y=\"%.7g\" z=\"%.7g\"/>\n", p.x, p.y, p.z); model.append(b, n); }
    model += "</vertices>\n<triangles>\n";
    for (size_t t = 0; t + 2 < idx.size(); t += 3) { int n = snprintf(b, sizeof b, "<triangle v1=\"%u\" v2=\"%u\" v3=\"%u\"/>\n", idx[t], idx[t + 1], idx[t + 2]); model.append(b, n); }
    model += "</triangles>\n</mesh>\n</object>\n</resources>\n<build>\n<item objectid=\"1\"/>\n</build>\n</model>\n";
    struct E { std::string name, data; uint32_t crc, off; };
    std::vector<E> files = {{"[Content_Types].xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>", 0, 0},
                            {"_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>", 0, 0},
                            {"3D/3dmodel.model", model, 0, 0}};
    std::string z, cd;
    for (auto& f : files) {
        f.crc = crc32(f.data); f.off = (uint32_t)z.size();
        put(z, (uint32_t)0x04034b50); put(z, (uint16_t)20); put(z, (uint16_t)0); put(z, (uint16_t)0); put(z, (uint16_t)0); put(z, (uint16_t)0x21); put(z, f.crc);
        put(z, (uint32_t)f.data.size()); put(z, (uint32_t)f.data.size()); put(z, (uint16_t)f.name.size()); put(z, (uint16_t)0); z += f.name; z += f.data;
    }
    for (auto& f : files) {
        put(cd, (uint32_t)0x02014b50); put(cd, (uint16_t)20); put(cd, (uint16_t)20); put(cd, (uint16_t)0); put(cd, (uint16_t)0); put(cd, (uint16_t)0); put(cd, (uint16_t)0x21); put(cd, f.crc);
        put(cd, (uint32_t)f.data.size()); put(cd, (uint32_t)f.data.size()); put(cd, (uint16_t)f.name.size()); put(cd, (uint16_t)0); put(cd, (uint16_t)0); put(cd, (uint16_t)0); put(cd, (uint16_t)0); put(cd, (uint32_t)0); put(cd, f.off); cd += f.name;
    }
    const uint32_t cdOff = (uint32_t)z.size(); z += cd;
    put(z, (uint32_t)0x06054b50); put(z, (uint16_t)0); put(z, (uint16_t)0); put(z, (uint16_t)files.size()); put(z, (uint16_t)files.size()); put(z, (uint32_t)cd.size()); put(z, cdOff); put(z, (uint16_t)0);
    return sys::writeFile(path, z) || fail(err, "Cannot write " + path);
}

// ------------------------------------------------------------------ STEP: a faceted B-rep (AP203/AP214 faceted_brep): one planar face (FACE_SURFACE on a PLANE, bounded by a POLY_LOOP) per triangle. Units: millimetres.
bool saveStep(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idxIn, std::string& err) {
    std::vector<uint32_t> idx; idx.reserve(idxIn.size());                                  // triangles without area have no plane: left out
    for (size_t t = 0; t + 2 < idxIn.size(); t += 3) { const Vec3 &a = v[idxIn[t]], &b = v[idxIn[t + 1]], &c = v[idxIn[t + 2]]; if (length(cross(b - a, c - a)) > 1e-12f && length(b - a) > 1e-9f) idx.insert(idx.end(), {idxIn[t], idxIn[t + 1], idxIn[t + 2]}); }
    if (idx.empty()) return fail(err, "There are no triangles with an area to write.");
    const std::string name = stemOf(path); time_t now = time(nullptr); char date[40]; strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", gmtime(&now));
    std::string s; s.reserve(v.size() * 56 + idx.size() / 3 * 420 + 3000); char b[500]; auto L = [&](const char* fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap); s.append(b, n); s += "\n"; };
    L("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('Dopplers 3D Curtain: faceted solid'),'2;1');\nFILE_NAME('%s.step','%s',(''),(''),'Dopplers 3D Curtain','Dopplers 3D Curtain','');\nFILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;", name.c_str(), date);
    L("#1=APPLICATION_CONTEXT('automotive design');\n#2=APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#1);\n#3=PRODUCT_CONTEXT('',#1,'mechanical');");
    L("#4=PRODUCT('%s','%s','',(#3));\n#5=PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4));\n#6=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design');", name.c_str(), name.c_str());
    L("#7=PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE('',$,#4,.NOT_KNOWN.);\n#8=PRODUCT_DEFINITION('design','',#7,#6);\n#9=PRODUCT_DEFINITION_SHAPE('','',#8);\n#10=SHAPE_DEFINITION_REPRESENTATION(#9,#11);");
    L("#11=FACETED_BREP_SHAPE_REPRESENTATION('%s',(#12,#20),#15);\n#12=AXIS2_PLACEMENT_3D('',#13,#14,$);\n#13=CARTESIAN_POINT('',(0.,0.,0.));\n#14=DIRECTION('',(0.,0.,1.));", name.c_str());
    L("#15=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#19)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#16,#17,#18)) REPRESENTATION_CONTEXT('Context3D','3D Context'));");
    L("#16=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));\n#17=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));\n#18=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());\n#19=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-06),#16,'distance_accuracy_value','confusion accuracy');");
    const size_t T = idx.size() / 3, pBase = 100, fBase = pBase + v.size(), shell = fBase + 7 * T;                // entity numbers; per triangle 7 entities
    L("#20=FACETED_BREP('%s',#%zu);", name.c_str(), shell);
    for (size_t i = 0; i < v.size(); i++) L("#%zu=CARTESIAN_POINT('',(%.7g,%.7g,%.7g));", pBase + i, v[i].x, v[i].y, v[i].z);
    for (size_t t = 0; t < T; t++) {
        const uint32_t i0 = idx[t * 3], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2]; const Vec3 n = normalized(cross(v[i1] - v[i0], v[i2] - v[i0])), r = normalized(v[i1] - v[i0]); const size_t e = fBase + 7 * t;
        L("#%zu=POLY_LOOP('',(#%zu,#%zu,#%zu));", e, pBase + i0, pBase + i1, pBase + i2);
        L("#%zu=FACE_OUTER_BOUND('',#%zu,.T.);", e + 1, e);
        L("#%zu=FACE_SURFACE('',(#%zu),#%zu,.T.);", e + 2, e + 1, e + 3);
        L("#%zu=PLANE('',#%zu);", e + 3, e + 4);
        L("#%zu=AXIS2_PLACEMENT_3D('',#%zu,#%zu,#%zu);", e + 4, pBase + i0, e + 5, e + 6);
        L("#%zu=DIRECTION('',(%.7g,%.7g,%.7g));", e + 5, n.x, n.y, n.z);
        L("#%zu=DIRECTION('',(%.7g,%.7g,%.7g));", e + 6, r.x, r.y, r.z);
    }
    s += "#" + std::to_string(shell) + "=CLOSED_SHELL('',(";
    for (size_t t = 0; t < T; t++) { s += "#" + std::to_string(fBase + 7 * t + 2); s += t + 1 < T ? "," : ""; if (t % 12 == 11) s += "\n"; }
    s += "));\nENDSEC;\nEND-ISO-10303-21;\n";
    return sys::writeFile(path, s) || fail(err, "Cannot write " + path);
}}  // namespace

bool saveMeshAny(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    if (!check(v, idx, err)) return false;
    const std::string e = extOf(path);
    if (e == "obj" || e.empty()) return saveObj(path, v, idx, err);
    if (e == "stl") return saveStl(path, v, idx, err);
    if (e == "ply") return savePly(path, v, idx, err);
    if (e == "glb") return saveGlb(path, v, idx, err);
    if (e == "gltf") return saveGltf(path, v, idx, err);
    if (e == "3mf") return save3mf(path, v, idx, err);
    if (e == "off") return saveOff(path, v, idx, err);
    if (e == "step" || e == "stp") return saveStep(path, v, idx, err);
    return fail(err, "Unknown output format ." + e + " (STL, OBJ, PLY, GLB, glTF, 3MF, OFF, STEP)");
}
