// Makes a real CAD solid with OpenCascade for testing the STEP import: a box with a cylindrical hole, plus a sphere on top (curved faces, trimmed faces, a boolean result).
// Prints the exact volume so that the imported mesh can be compared. Build: see CMakeLists (target step_make).
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Writer.hxx>
#include <cstdio>
int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "cad_test.step";
    TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-30, -20, 0), 60, 40, 20).Shape();
    TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp_Dir(0, 0, 1)), 8, 22).Shape();
    TopoDS_Shape part = BRepAlgoAPI_Cut(box, hole).Shape();
    TopoDS_Shape ball = BRepPrimAPI_MakeSphere(gp_Pnt(18, 0, 20), 10).Shape();
    TopoDS_Shape all = BRepAlgoAPI_Fuse(part, ball).Shape();
    GProp_GProps props; BRepGProp::VolumeProperties(all, props);
    STEPControl_Writer w; w.Transfer(all, STEPControl_AsIs); if (w.Write(out) != IFSelect_RetDone) { printf("write failed\n"); return 1; }
    printf("wrote %s, exact volume %.3f\n", out, props.Mass());
    return 0;
}
