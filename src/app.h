// Shared application state: documents (model tabs and image tabs), the active tool, and the hooks the 3D view calls into the main window.
#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "curtain.h"
#include "mesh.h"

enum class Tool { None, Distance, Precision, Density, Scale, Rotate, Move, Details };

struct DocState {
    MeshPtr mesh; Xform xf; CurtainParams cp;
    bool operator==(const DocState& o) const { return mesh == o.mesh && xf == o.xf && cp == o.cp; }
};

struct Camera { float yaw = -50, pitch = 28, dist = 5; Vec3 target; };

struct Doc {
    enum Kind { Model, Image } kind = Model;
    uint64_t id = 0;
    std::string path, title; bool dirty = false;
    virtual ~Doc() {}
};

struct ModelDoc : Doc {
    ModelDoc() { kind = Model; }
    DocState cur;
    std::vector<DocState> undo, redo;
    static const size_t kMaxUndo = 200;
    Vec3 pivot;                                        // centre of the base mesh bounding box
    std::vector<Vec3> world, wnormal; Vec3 wlo, whi;   // the mesh in the world (transform applied), its normals and bounds
    Camera cam;
    // the curtain for the current shape and parameters (kept while the object is not moved; computed in the background)
    std::shared_ptr<CurtainResult> curtain; CurtainParams curtainParams; uint64_t geomVer = 1, curtainGeomVer = 0; bool curtainBusy = false;
    float alpha = 0.35f; bool showCurtain = false, showVertices = true, hideObject = false;   // hideObject: only the wrap is drawn
    DocState editBefore; bool editing = false;         // set while a drag or a slider is being used: committed to the undo list at the end

    void setMesh(MeshPtr m);                           // new base mesh: resets the pivot, puts the object on the plate
    void rebuild();                                    // world vertices, normals, bounds from cur
    void dropToPlate();                                // moves the object so that its lowest point is on the plate
    void centerOnPlate();
    void fitCamera();
    void beginEdit() { if (!editing) { editBefore = cur; editing = true; } }
    bool endEdit();                                    // pushes the undo step if something changed; true when it did
    bool doUndo(); bool doRedo();
    float diag() const { return length(whi - wlo); }
    Vec3 center() const { return (wlo + whi) * 0.5f; }
};

struct ImageDoc : Doc {
    ImageDoc() { kind = Image; }
    HBITMAP bmp = nullptr; int w = 0, h = 0;
    int seed = 0, steps = 20, resolution = 256; float cfg = 8.0f, threshold = 0.7f; bool randomSeed = true;
    ~ImageDoc() { if (bmp) DeleteObject(bmp); }
};

// ---- the 3D view (view.cpp)
HWND createView(HWND parent, HINSTANCE inst);
void viewRedraw();
bool viewSnapshot(const std::string& path);
void viewSetPreset(const char* name);                 // "front", "back", "left", "right", "top", "bottom", "center"

// ---- hooks implemented by the main window (main.cpp)
ModelDoc* activeModel();
Tool activeTool();
void onEditChanged(ModelDoc* d, bool commit);         // the object or curtain parameters changed through the view (commit: the drag ended)
void onGeometryChanged(ModelDoc* d);                  // world geometry changed: the curtain is out of date
void requestCurtain(ModelDoc* d);
bool curtainWanted(ModelDoc* d);                      // visible, or a curtain tool is open
const char* APP_NAME();
