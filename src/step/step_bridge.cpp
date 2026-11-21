// d3c_step.dll: reads a STEP / STP file with OpenCascade and returns it as one triangle mesh. A small C interface, so that the programs do not link OpenCascade
// themselves (they load this DLL when a STEP file is opened). OpenCascade is LGPL 2.1 with the OCCT exception; its DLLs sit next to this one.
#include <windows.h>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLib.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <unordered_map>
#include <cmath>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
// deflection: the largest distance between the true surface and the triangles, as a fraction of the bounding-box diagonal (0.0003 is fine for display)
// returns 0 on success; the arrays are owned by the DLL's allocator: free them with d3c_step_free
__declspec(dllexport) int d3c_step_read(const char* utf8Path, double deflection, float** verts, int* nv, unsigned** idx, int* nt, char* err, int errLen) {
    auto fail = [&](const char* m) { if (err && errLen > 0) snprintf(err, (size_t)errLen, "%s", m); return 1; };
    try {
        STEPControl_Reader reader;
        if (reader.ReadFile(utf8Path) != IFSelect_RetDone) return fail("OpenCascade could not read the file (is it a STEP file?).");
        if (reader.TransferRoots() <= 0) return fail("The STEP file holds no shapes that could be transferred.");
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) return fail("The STEP file holds no solid or surface.");
        Bnd_Box box; BRepBndLib::Add(shape, box); if (box.IsVoid()) return fail("The shape is empty.");
        double x0, y0, z0, x1, y1, z1; box.Get(x0, y0, z0, x1, y1, z1);
        const double diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
        BRepMesh_IncrementalMesh mesher(shape, std::max(diag * deflection, 1e-9), false, 0.35, true);          // linear deflection, absolute; angular 20 degrees; parallel
        std::vector<float> V; std::vector<unsigned> I;
        for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face face = TopoDS::Face(ex.Current()); TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc); if (tri.IsNull()) continue;
            const unsigned base = (unsigned)(V.size() / 3); const gp_Trsf tr = loc.Transformation(); const bool rev = face.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= tri->NbNodes(); i++) { gp_Pnt p = tri->Node(i).Transformed(tr); V.push_back((float)p.X()); V.push_back((float)p.Y()); V.push_back((float)p.Z()); }
            for (int t = 1; t <= tri->NbTriangles(); t++) {
                int a, b, c; tri->Triangle(t).Get(a, b, c); if (rev) std::swap(b, c);
                I.push_back(base + a - 1); I.push_back(base + b - 1); I.push_back(base + c - 1);
            }
        }
        if (I.empty()) return fail("The shape could not be turned into triangles.");
        *nv = (int)(V.size() / 3); *nt = (int)(I.size() / 3);
        *verts = (float*)malloc(V.size() * sizeof(float)); *idx = (unsigned*)malloc(I.size() * sizeof(unsigned));
        memcpy(*verts, V.data(), V.size() * sizeof(float)); memcpy(*idx, I.data(), I.size() * sizeof(unsigned));
        return 0;
    } catch (Standard_Failure const& f) { char b[300]; snprintf(b, sizeof b, "OpenCascade: %s", f.GetMessageString() ? f.GetMessageString() : "error"); return fail(b); }
    catch (...) { return fail("OpenCascade failed on this file."); }
}
// writes a closed triangle mesh as a real CAD solid: planar faces on shared vertices and edges, one shell, one MANIFOLD_SOLID_BREP with ADVANCED_FACEs
__declspec(dllexport) int d3c_step_write(const char* utf8Path, const float* verts, int nv, const unsigned* idx, int nt, char* err, int errLen) {
    auto fail = [&](const char* m) { if (err && errLen > 0) snprintf(err, (size_t)errLen, "%s", m); return 1; };
    try {
        BRep_Builder bb; std::vector<TopoDS_Vertex> V((size_t)nv);
        for (int i = 0; i < nv; i++) V[(size_t)i] = BRepBuilderAPI_MakeVertex(gp_Pnt(verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]));
        std::unordered_map<unsigned long long, TopoDS_Edge> edges; edges.reserve((size_t)nt * 2);
        auto edgeOf = [&](unsigned a, unsigned b, bool& fwd) -> TopoDS_Edge {                       // one edge per vertex pair, built from the lower index to the higher
            const unsigned lo = a < b ? a : b, hi = a < b ? b : a; fwd = a < b; const unsigned long long k = ((unsigned long long)lo << 32) | hi;
            auto it = edges.find(k); if (it != edges.end()) return it->second;
            TopoDS_Edge e = BRepBuilderAPI_MakeEdge(V[lo], V[hi]); edges[k] = e; return e; };
        TopoDS_Shell shell; bb.MakeShell(shell); int faces = 0;
        for (int t = 0; t < nt; t++) {
            const unsigned a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2]; if (a == b || b == c || a == c) continue;
            const float *pa = verts + a * 3, *pb = verts + b * 3, *pc = verts + c * 3;
            const double ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2], vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx; if (std::sqrt(nx * nx + ny * ny + nz * nz) < 1e-12) continue;
            TopoDS_Wire w; bb.MakeWire(w); const unsigned loop[3][2] = {{a, b}, {b, c}, {c, a}};
            for (auto& l : loop) { bool fwd; TopoDS_Edge e = edgeOf(l[0], l[1], fwd); bb.Add(w, fwd ? e : TopoDS_Edge(TopoDS::Edge(e.Reversed()))); }
            w.Closed(true);
            BRepBuilderAPI_MakeFace mf(w, true); if (!mf.IsDone()) continue; TopoDS_Face f = mf.Face();
            BRepAdaptor_Surface s(f); gp_Dir fn = s.Plane().Axis().Direction(); if (f.Orientation() == TopAbs_REVERSED) fn.Reverse();   // the face must look the way the triangle winds
            if (fn.X() * nx + fn.Y() * ny + fn.Z() * nz < 0) f.Reverse();
            bb.Add(shell, f); faces++;
        }
        if (faces == 0) return fail("There are no triangles with an area to write.");
        shell.Closed(true); TopoDS_Solid solid; bb.MakeSolid(solid); bb.Add(solid, shell);
        BRepLib::OrientClosedSolid(solid);
        STEPControl_Writer wr; if (wr.Transfer(solid, STEPControl_AsIs) != IFSelect_RetDone) return fail("OpenCascade could not convert the mesh to a solid.");
        if (wr.Write(utf8Path) != IFSelect_RetDone) return fail("OpenCascade could not write the file.");
        return 0;
    } catch (Standard_Failure const& f) { char b[300]; snprintf(b, sizeof b, "OpenCascade: %s", f.GetMessageString() ? f.GetMessageString() : "error"); return fail(b); }
    catch (...) { return fail("OpenCascade failed while writing."); }
}
__declspec(dllexport) void d3c_step_free(void* p) { free(p); }
}
