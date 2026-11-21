// The "curtain": a smooth shell drawn around the object at a chosen distance, kept as a signed distance field on a regular grid.
// The field is what a game engine needs: one trilinear lookup (8 reads) answers "how far is this point from the curtain" (negative inside).
#pragma once
#include <atomic>
#include <string>
#include <vector>
#include "mesh.h"

enum class CurtainMode : int {
    Smooth = 0,       // offset of the object, blended towards a clean sphere as the distance grows ("Sphere limit - Smooth")
    Hull = 1          // offset of the object's convex hull ("Convex hull - Distance")
};

struct CurtainParams {
    CurtainMode mode = CurtainMode::Smooth;
    float distance = 0.05f;        // how far the curtain is from the object, as a fraction of the object's bounding-box diagonal
    int precision = 128;           // fidelity: grid cells along the longest side. High = a wrap that follows every curve like vinyl, low = the wrap cuts the corners
    int density = 30000;           // how many vertices the wrap mesh has (independent of the precision): fewer are merged, more are added on the same surface
    bool operator==(const CurtainParams& o) const { return mode == o.mode && distance == o.distance && precision == o.precision && density == o.density; }
};

struct CurtainField {
    int n[3] = {0, 0, 0};
    Vec3 origin;                   // world position of grid point (0,0,0)
    float h = 1;                   // grid spacing
    float level = 0;               // the curtain distance in world units
    std::vector<float> g;          // signed distance to the curtain surface (negative inside), one value per grid point, x fastest
    bool empty() const { return g.empty(); }
    float eval(const Vec3& p) const;   // trilinear
};

struct CurtainResult {
    CurtainField field;
    std::vector<Vec3> v, nrm;      // the curtain surface (surface nets), for display and export
    std::vector<uint32_t> idx;
    std::vector<Vec3> hullV;       // convex hull of the object (always computed in Hull mode)
    std::vector<uint32_t> hullIdx;
    double seconds = 0;
};

// world-space triangles in; false when cancelled or the object is empty
bool computeCurtain(const std::vector<Vec3>& wv, const std::vector<uint32_t>& idx, const CurtainParams& p, CurtainResult& out, const std::atomic<bool>* cancel);
const int kMaxPrecision = 384, kMinPrecision = 16, kMinDensity = 200, kMaxDensity = 2000000;
bool convexHull(const std::vector<Vec3>& pts, std::vector<Vec3>& hv, std::vector<uint32_t>& hidx);

// engine exports of the field (16-bit values, trilinear lookup)
std::string curtainHeaderText(const CurtainField& f, const std::string& name);
std::string curtainBlob(const CurtainField& f);                 // "CVF1" + dims + origin + spacing + scale + int16 data
bool curtainFieldFromBlob(const std::string& blob, CurtainField& f);
