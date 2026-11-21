// The .curtain project file: the mesh, the transform, the curtain parameters and the fitted distance field (what the engine reads).
#pragma once
#include <string>
#include "app.h"

bool saveProject(const ModelDoc& d, const CurtainResult* curtain, const std::string& path, std::string& err);   // curtain: its field is stored (may be null)
bool loadProject(const std::string& path, ModelDoc& d, std::string& err);                                       // mesh, transform and parameters; the curtain is recomputed
