// Picture -> 3D model with the Hunyuan3D 2.0 shape network (no ComfyUI): image encoder, flow-matching DiT with classifier-free guidance, shape decoder, surface extraction.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "mesh.h"

struct H3DParams {
    unsigned seed = 0; int steps = 20; float cfg = 8.0f; int resolution = 256; float threshold = 0.7f;
    bool stretch = false;                    // true: squash the picture to a square like the ComfyUI workflow; false: pad it with white to keep the proportions
    std::string checkpoint;                  // hunyuan3d-dit-v2_fp16.safetensors
    std::string noiseFile;                   // tests: use this 64 x 3072 float32 noise instead of the seed
    std::string dumpDir;                     // tests: write the final latent and the distance grid here
};
// progress(stage text, fraction 0..1) returns false to cancel
using H3DProgress = std::function<bool(const std::string&, float)>;

// RGB 8-bit image from any common file (alpha is composited onto white)
bool loadRgbImage(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgb, std::string& err);
// the preprocessing of the image encoder (bicubic resize with antialias like PyTorch, 8-bit rounding, ImageNet normalisation): 3 x 518 x 518
void dinoPixels(const std::vector<uint8_t>& rgb, int w, int h, bool stretch, std::vector<float>& out);
bool imageToMesh(const std::string& imagePath, const H3DParams& p, Mesh& out, const H3DProgress& progress, std::string& err);
void h3dSigmas(int steps, std::vector<float>& sigmas);       // the "normal" flow schedule: 1.0 .. 0.001, then 0
