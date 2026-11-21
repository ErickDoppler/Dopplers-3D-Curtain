// Triangle mesh, the object transform, and the file formats (OBJ, STL, PLY, glTF/GLB in; OBJ, GLB out).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "vmath.h"

struct Mesh {
    std::vector<Vec3> v;
    std::vector<uint32_t> idx;                      // 3 per triangle
    size_t triCount() const { return idx.size() / 3; }
    void bounds(Vec3& lo, Vec3& hi) const;
};
using MeshPtr = std::shared_ptr<const Mesh>;

// The object sits on the plate. Scale and rotation act about the centre of the mesh bounding box (the pivot); pos is where the pivot is in the world.
struct Xform {
    Vec3 pos, rotDeg, scale{1, 1, 1};
    bool operator==(const Xform& o) const { return pos.x == o.pos.x && pos.y == o.pos.y && pos.z == o.pos.z && rotDeg.x == o.rotDeg.x && rotDeg.y == o.rotDeg.y && rotDeg.z == o.rotDeg.z && scale.x == o.scale.x && scale.y == o.scale.y && scale.z == o.scale.z; }
};
Mat4 xformMatrix(const Xform& x, const Vec3& pivot);          // mesh space -> world
void transformMesh(const Mesh& m, const Mat4& M, std::vector<Vec3>& out);

// file formats; the format comes from the extension
bool loadMeshFile(const std::string& path, Mesh& out, std::string& err);
bool saveObj(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err);
bool saveGlb(const std::string& path, const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::string& err);   // written Y-up like the format says
void computeNormals(const std::vector<Vec3>& v, const std::vector<uint32_t>& idx, std::vector<Vec3>& n);
// The "Object details" operation: vertices falling into removed cells of a 10000^3 grid over the bounding box are merged into the nearest kept neighbour.
std::shared_ptr<Mesh> reduceDetails(const Mesh& m, double qualityPercent);

// The eraser: removes the marked vertices and every triangle that used them, then closes each hole. For every hole the ring of vertices that were
// connected to the removed ones is collected and triangulated (ear clipping in the plane of the ring), so the new ribs run between those neighbours.
// Unused vertices are dropped. holesClosed (optional) receives the number of rings that were closed.
std::shared_ptr<Mesh> eraseVertices(const Mesh& m, const std::vector<uint8_t>& del, size_t* holesClosed = nullptr);
