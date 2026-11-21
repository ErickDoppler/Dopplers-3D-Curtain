// d3c_step.dll: reads a STEP / STP file with OpenCascade and returns it as one triangle mesh. A small C interface, so that the programs do not link OpenCascade
// themselves (they load this DLL when a STEP file is opened). OpenCascade is LGPL 2.1 with the OCCT exception; its DLLs sit next to this one.
#include <windows.h>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
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
__declspec(dllexport) void d3c_step_free(void* p) { free(p); }
}
