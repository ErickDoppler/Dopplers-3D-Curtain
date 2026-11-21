#include "app.h"

static Vec3 baseCenter(const Mesh& m) { Vec3 lo, hi; m.bounds(lo, hi); return (lo + hi) * 0.5f; }

void ModelDoc::rebuild() {
    if (!cur.mesh) { world.clear(); wnormal.clear(); wlo = whi = Vec3(); geomVer++; return; }
    transformMesh(*cur.mesh, xformMatrix(cur.xf, pivot), world);
    computeNormals(world, cur.mesh->idx, wnormal);
    wlo = whi = world.empty() ? Vec3() : world[0];
    for (auto& p : world) { wlo = vmin(wlo, p); whi = vmax(whi, p); }
    geomVer++;
}

void ModelDoc::dropToPlate() {
    const float dz = wlo.z; if (dz == 0) return;
    for (auto& p : world) p.z -= dz;
    wlo.z -= dz; whi.z -= dz; cur.xf.pos.z -= dz; geomVer++;
}

void ModelDoc::centerOnPlate() {
    cur.xf.pos.x = 0; cur.xf.pos.y = 0; rebuild(); dropToPlate();
}

void ModelDoc::setMesh(MeshPtr m) {
    cur.mesh = m; pivot = baseCenter(*m);
    cur.xf = Xform(); cur.xf.pos = pivot;
    centerOnPlate(); fitCamera();
}

void ModelDoc::fitCamera() {
    const float d = std::max(diag(), 1e-6f);
    cam.target = center(); cam.dist = d * 1.25f / std::tan(rad(22.5f)) * 0.5f + d * 0.5f;
}

bool ModelDoc::endEdit() {
    if (!editing) return false;
    editing = false;
    if (cur == editBefore) return false;
    undo.push_back(editBefore); if (undo.size() > kMaxUndo) undo.erase(undo.begin());
    redo.clear(); dirty = true; return true;
}

static void applyState(ModelDoc& d, const DocState& s) {
    const bool newMesh = d.cur.mesh != s.mesh; d.cur = s;
    if (newMesh && s.mesh) d.pivot = baseCenter(*s.mesh);
    d.rebuild();
}
bool ModelDoc::doUndo() {
    if (undo.empty()) return false;
    redo.push_back(cur); DocState s = undo.back(); undo.pop_back(); applyState(*this, s); dirty = true; return true;
}
bool ModelDoc::doRedo() {
    if (redo.empty()) return false;
    undo.push_back(cur); DocState s = redo.back(); redo.pop_back(); applyState(*this, s); dirty = true; return true;
}
