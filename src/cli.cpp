// d3c - the command line of Doppler's 3D Curtain (console program, same core as the window). Made to be driven by scripts and by Claude Code:
// plain text by default, one JSON object with --json, exit code 0 = success.
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "app.h"
#include "curtain.h"
#include "json.h"
#include "mesh.h"
#include "project.h"
#include "sys.h"

namespace {
struct Args {
    std::vector<std::string> pos; std::map<std::string, std::string> opt;
    bool has(const std::string& k) const { return opt.count(k) > 0; }
    std::string get(const std::string& k, const std::string& d = "") const { auto it = opt.find(k); return it == opt.end() ? d : it->second; }
    double num(const std::string& k, double d) const { auto it = opt.find(k); return it == opt.end() ? d : atof(it->second.c_str()); }
};
bool g_json = false;
Json g_out = Json::object();

int fail(const std::string& msg, int code = 1) {
    if (g_json) { Json j = Json::object(); j["ok"] = false; j["error"] = msg; printf("%s\n", j.dump(2).c_str()); }
    else fprintf(stderr, "error: %s\n", msg.c_str());
    return code;
}
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string extOf(const std::string& p) { size_t k = p.find_last_of('.'); return k == std::string::npos ? "" : lower(p.substr(k + 1)); }
std::string baseOf(const std::string& p) { size_t k = p.find_last_of("\\/"); std::string b = k == std::string::npos ? p : p.substr(k + 1); size_t d = b.find_last_of('.'); return d == std::string::npos ? b : b.substr(0, d); }
Vec3 vec3(const std::string& s, const Vec3& def, bool allSame) {                                  // "50" or "50,60,70"
    if (s.empty()) return def; float v[3] = {0, 0, 0}; int n = sscanf(s.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]);
    if (n == 1 && allSame) return Vec3(v[0], v[0], v[0]); if (n == 1) return Vec3(v[0], def.y, def.z);
    if (n == 2) return Vec3(v[0], v[1], def.z); return Vec3(v[0], v[1], v[2]);
}
Json jv(const Vec3& v) { Json a = Json::array(); a.push(v.x); a.push(v.y); a.push(v.z); return a; }
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) { if (g_json) return; va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

void edgeStats(const std::vector<uint32_t>& idx, size_t& open, size_t& nonManifold) {
    std::unordered_map<uint64_t, int> e; e.reserve(idx.size());
    for (size_t t = 0; t + 2 < idx.size(); t += 3) for (int k = 0; k < 3; k++) { uint32_t a = idx[t + k], b = idx[t + (k + 1) % 3]; e[((uint64_t)std::min(a, b) << 32) | std::max(a, b)]++; }
    open = nonManifold = 0; for (auto& kv : e) { if (kv.second == 1) open++; else if (kv.second > 2) nonManifold++; }
}

// loads a model file or a .curtain project into a document placed on the plate
bool loadDoc(const std::string& path, ModelDoc& d, std::string& err) {
    if (extOf(path) == "curtain") return loadProject(path, d, err);
    auto m = std::make_shared<Mesh>(); if (!loadMeshFile(path, *m, err)) return false;
    d.setMesh(m); return true;
}
bool saveMeshAny(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err) {
    const std::string e = extOf(path); if (e == "glb") return saveGlb(path, v, idx, err); if (e == "obj" || e.empty()) return saveObj(path, v, idx, err);
    err = "Unknown output format ." + e + " (use .obj or .glb)"; return false;
}

int usage() {
    printf(
        "d3c - command line of Doppler's 3D Curtain\n\n"
        "  d3c info <model>                     vertices, triangles, size, open edges\n"
        "  d3c curtain <model|project.curtain> [options]\n"
        "        --mode smooth|hull             smooth = sphere limit (default), hull = convex hull + distance\n"
        "        --distance <percent>           curtain distance, percent of the object's bounding-box diagonal (default 5)\n"
        "        --precision <16..384>          fidelity: grid cells along the longest side. High = follows every curve, low = cuts the corners (default 128)\n"
        "        --density <vertices>           vertices in the wrap mesh, independent of the precision (default 30000)\n"
        "        --scale <pct | x,y,z>          object size in percent per axis        --rotate <x,y,z>   degrees about the horizon axes\n"
        "        --out <wrap.obj|.glb>          the wrap surface                       --object-out <file.obj|.glb>   the (transformed) object\n"
        "        --header <file.h> [--name id]  engine header with an evaluate function (one trilinear lookup)\n"
        "        --cvf <file.cvf>               engine binary distance field           --project <file.curtain>   project file\n"
        "  d3c reduce <model> --quality <percent> --out <file.obj|.glb>     object details: keep that share of the vertices\n"
        "  d3c convert <in> <out>               OBJ / STL / PLY / glTF / GLB in; OBJ / GLB out\n"
        "  d3c eval <file.cvf> <x> <y> <z> ...  signed distance of the curtain at points (negative inside)\n"
        "  d3c image2mesh <image> ...           picture to 3D (Hunyuan3D): not built into this version yet\n\n"
        "  --json (anywhere) prints one JSON object instead of text. Exit code 0 = success.\n"
        "  Units are the model's own. Z is up; the object is put on the plate (lowest point at z = 0, centred in x and y).\n");
    return 0;
}

int cmdInfo(const Args& a) {
    if (a.pos.size() < 2) return fail("info needs a model file");
    ModelDoc d; std::string err; if (!loadDoc(a.pos[1], d, err)) return fail(err);
    size_t open, nm; edgeStats(d.cur.mesh->idx, open, nm); const Vec3 s = d.whi - d.wlo;
    say("file       %s\nvertices   %zu\ntriangles  %zu\nsize       %.6g x %.6g x %.6g   (diagonal %.6g)\nopen edges %zu   non-manifold edges %zu\n", a.pos[1].c_str(), d.cur.mesh->v.size(), d.cur.mesh->triCount(), s.x, s.y, s.z, d.diag(), open, nm);
    g_out["ok"] = true; g_out["file"] = a.pos[1]; g_out["vertices"] = (long long)d.cur.mesh->v.size(); g_out["triangles"] = (long long)d.cur.mesh->triCount();
    g_out["size"] = jv(s); g_out["diagonal"] = d.diag(); g_out["open_edges"] = (long long)open; g_out["non_manifold_edges"] = (long long)nm; g_out["watertight"] = open == 0 && nm == 0;
    return 0;
}

int cmdCurtain(const Args& a) {
    if (a.pos.size() < 2) return fail("curtain needs a model file or a .curtain project");
    ModelDoc d; std::string err; if (!loadDoc(a.pos[1], d, err)) return fail(err);
    CurtainParams& cp = d.cur.cp;
    if (a.has("mode")) { const std::string m = lower(a.get("mode")); if (m == "hull" || m == "convex" || m == "convex-hull") cp.mode = CurtainMode::Hull; else if (m == "smooth" || m == "sphere") cp.mode = CurtainMode::Smooth; else return fail("--mode must be smooth or hull"); }
    if (a.has("distance")) cp.distance = (float)(a.num("distance", 5) / 100.0);
    if (a.has("precision")) cp.precision = (int)a.num("precision", 128);
    if (a.has("density")) cp.density = (int)a.num("density", 30000);
    if (cp.distance < 0 || cp.distance > 3) return fail("--distance is a percent between 0 and 300");
    if (cp.precision < kMinPrecision || cp.precision > kMaxPrecision) return fail("--precision must be between " + std::to_string(kMinPrecision) + " and " + std::to_string(kMaxPrecision));
    if (cp.density < kMinDensity || cp.density > kMaxDensity) return fail("--density must be between " + std::to_string(kMinDensity) + " and " + std::to_string(kMaxDensity));
    if (a.has("scale") || a.has("rotate")) {
        if (a.has("scale")) { Vec3 s = vec3(a.get("scale"), Vec3(100, 100, 100), true); d.cur.xf.scale = s / 100.0f; }
        if (a.has("rotate")) d.cur.xf.rotDeg = vec3(a.get("rotate"), Vec3(), false);
        d.rebuild(); d.dropToPlate();
    }
    CurtainResult r;
    if (!computeCurtain(d.world, d.cur.mesh->idx, cp, r, nullptr)) return fail("The curtain could not be computed (is the object empty, or the grid too large?)");
    d.curtain = std::make_shared<CurtainResult>(r);
    say("curtain     mode %s, distance %.3g %%, precision %d, density %d\ngrid        %d x %d x %d   cell %.6g\nwrap        %zu vertices, %zu triangles\ntime        %.2f s\n",
        cp.mode == CurtainMode::Hull ? "convex hull" : "smooth (sphere limit)", cp.distance * 100.0, cp.precision, cp.density, r.field.n[0], r.field.n[1], r.field.n[2], r.field.h, r.v.size(), r.idx.size() / 3, r.seconds);
    g_out["ok"] = true; g_out["mode"] = cp.mode == CurtainMode::Hull ? "hull" : "smooth"; g_out["distance_percent"] = cp.distance * 100.0; g_out["precision"] = cp.precision; g_out["density"] = cp.density;
    Json grid = Json::array(); for (int i = 0; i < 3; i++) grid.push(r.field.n[i]); g_out["grid"] = grid; g_out["cell"] = r.field.h; g_out["level"] = r.field.level;
    g_out["wrap_vertices"] = (long long)r.v.size(); g_out["wrap_triangles"] = (long long)(r.idx.size() / 3); g_out["seconds"] = r.seconds;
    g_out["object_vertices"] = (long long)d.cur.mesh->v.size(); g_out["object_size"] = jv(d.whi - d.wlo);
    size_t open, nm; edgeStats(r.idx, open, nm); g_out["wrap_open_edges"] = (long long)open;
    Json files = Json::array();
    auto wrote = [&](const std::string& what, const std::string& path) { say("wrote       %s: %s\n", what.c_str(), path.c_str()); Json f = Json::object(); f["what"] = what; f["path"] = path; files.push(f); };
    if (a.has("out")) { if (!saveMeshAny(a.get("out"), r.v, r.idx, err)) return fail(err); wrote("wrap surface", a.get("out")); }
    if (a.has("object-out")) { if (!saveMeshAny(a.get("object-out"), d.world, d.cur.mesh->idx, err)) return fail(err); wrote("object", a.get("object-out")); }
    if (a.has("header")) { std::string name = a.get("name", "curtain_" + baseOf(a.get("header"))); for (auto& c : name) if (!isalnum((unsigned char)c)) c = '_'; if (!sys::writeFile(a.get("header"), curtainHeaderText(r.field, name))) return fail("Cannot write " + a.get("header")); wrote("engine header (function " + name + "_eval)", a.get("header")); }
    if (a.has("cvf")) { if (!sys::writeFile(a.get("cvf"), curtainBlob(r.field))) return fail("Cannot write " + a.get("cvf")); wrote("engine field", a.get("cvf")); }
    if (a.has("project")) { if (!saveProject(d, &r, a.get("project"), err)) return fail(err); wrote("project", a.get("project")); }
    g_out["files"] = files;
    return 0;
}

int cmdReduce(const Args& a) {
    if (a.pos.size() < 2 || !a.has("quality") || !a.has("out")) return fail("usage: d3c reduce <model> --quality <percent> --out <file.obj|.glb>");
    auto m = std::make_shared<Mesh>(); std::string err; if (!loadMeshFile(a.pos[1], *m, err)) return fail(err);
    const double q = a.num("quality", 100); if (q < 0 || q > 100) return fail("--quality is a percent between 0 and 100");
    auto r = reduceDetails(*m, q); if (r->idx.empty()) return fail("Nothing would be left at that quality.");
    if (!saveMeshAny(a.get("out"), r->v, r->idx, err)) return fail(err);
    say("vertices   %zu -> %zu (%.2f %%)\ntriangles  %zu -> %zu\nwrote      %s\n", m->v.size(), r->v.size(), 100.0 * r->v.size() / m->v.size(), m->triCount(), r->triCount(), a.get("out").c_str());
    g_out["ok"] = true; g_out["vertices_before"] = (long long)m->v.size(); g_out["vertices_after"] = (long long)r->v.size(); g_out["triangles_before"] = (long long)m->triCount(); g_out["triangles_after"] = (long long)r->triCount(); g_out["out"] = a.get("out");
    return 0;
}

int cmdConvert(const Args& a) {
    if (a.pos.size() < 3) return fail("usage: d3c convert <in> <out>");
    Mesh m; std::string err; if (!loadMeshFile(a.pos[1], m, err)) return fail(err);
    if (!saveMeshAny(a.pos[2], m.v, m.idx, err)) return fail(err);
    say("%zu vertices, %zu triangles -> %s\n", m.v.size(), m.triCount(), a.pos[2].c_str());
    g_out["ok"] = true; g_out["vertices"] = (long long)m.v.size(); g_out["triangles"] = (long long)m.triCount(); g_out["out"] = a.pos[2];
    return 0;
}

int cmdEval(const Args& a) {
    if (a.pos.size() < 5 || (a.pos.size() - 2) % 3) return fail("usage: d3c eval <file.cvf> <x> <y> <z> [<x> <y> <z> ...]");
    std::string blob; if (!sys::readFile(a.pos[1], blob)) return fail("Cannot read " + a.pos[1]);
    CurtainField f; if (!curtainFieldFromBlob(blob, f)) return fail("Not a curtain field file (.cvf).");
    Json pts = Json::array();
    for (size_t i = 2; i + 2 < a.pos.size(); i += 3) {
        Vec3 p((float)atof(a.pos[i].c_str()), (float)atof(a.pos[i + 1].c_str()), (float)atof(a.pos[i + 2].c_str())); const float g = f.eval(p);
        say("(%.6g, %.6g, %.6g)  ->  %.6g  %s\n", p.x, p.y, p.z, g, g < 0 ? "inside the curtain" : "outside");
        Json o = Json::object(); o["point"] = jv(p); o["distance"] = g; o["inside"] = g < 0; pts.push(o);
    }
    g_out["ok"] = true; g_out["points"] = pts;
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--json") { g_json = true; continue; }
        if (s.size() > 2 && s[0] == '-' && s[1] == '-') { std::string k = s.substr(2), v; size_t eq = k.find('='); if (eq != std::string::npos) { v = k.substr(eq + 1); k = k.substr(0, eq); } else if (i + 1 < argc) v = argv[++i]; a.opt[k] = v; }
        else a.pos.push_back(s);
    }
    if (a.pos.empty() || a.pos[0] == "help" || a.pos[0] == "--help" || a.has("help")) return usage();
    const std::string c = a.pos[0]; int rc;
    if (c == "info") rc = cmdInfo(a); else if (c == "curtain") rc = cmdCurtain(a); else if (c == "reduce") rc = cmdReduce(a); else if (c == "convert") rc = cmdConvert(a); else if (c == "eval") rc = cmdEval(a);
    else if (c == "image2mesh") return fail("Image to 3D (Hunyuan3D) is not built into this version yet.", 3);
    else return fail("unknown command '" + c + "' (run d3c help)", 2);
    if (g_json && rc == 0) printf("%s\n", g_out.dump(2).c_str());
    return rc;
}
