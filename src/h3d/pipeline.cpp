#include "h3d/pipeline.h"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include "curtain.h"
#include "h3d/dino.h"
#include "h3d/dit.h"
#include "h3d/vae.h"
#include "sys.h"

// ------------------------------------------------------------------ picture in
bool loadRgbImage(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgb, std::string& err) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* f = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr; bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))
        && SUCCEEDED(f->CreateDecoderFromFilename(sys::widen(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr))
        && SUCCEEDED(f->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT uw, uh; cv->GetSize(&uw, &uh); w = (int)uw; h = (int)uh;
        std::vector<uint8_t> px((size_t)w * h * 4);
        if (SUCCEEDED(cv->CopyPixels(nullptr, (UINT)w * 4, (UINT)px.size(), px.data()))) {
            const bool keepRaw = getenv("D3C_IGNORE_ALPHA") != nullptr;                         // tests: like ComfyUI, which drops the alpha channel
            rgb.resize((size_t)w * h * 3);
            for (size_t i = 0; i < (size_t)w * h; i++) { const int a = keepRaw ? 255 : px[i * 4 + 3]; for (int c = 0; c < 3; c++) rgb[i * 3 + c] = (uint8_t)((px[i * 4 + c] * a + 255 * (255 - a) + 127) / 255); }   // transparent parts become white
            ok = true;
        }
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); if (f) f->Release();
    if (!ok) err = "Cannot read the picture " + path;
    return ok;
}

namespace {
float cubic(float x) { x = std::fabs(x); const float a = -0.5f; if (x < 1) return ((a + 2) * x - (a + 3)) * x * x + 1; if (x < 2) return (((x - 5) * x + 8) * x - 4) * a; return 0; }
struct Tap { int min, n; std::vector<float> w; };
std::vector<Tap> taps(int in, int out) {                                                  // PyTorch upsample_bicubic2d with antialias
    std::vector<Tap> t(out); const float scale = (float)in / out, support = scale >= 1.0f ? 2.0f * scale : 2.0f, inv = scale >= 1.0f ? 1.0f / scale : 1.0f;
    for (int i = 0; i < out; i++) {
        const float center = scale * (i + 0.5f); const int xmin = std::max((int)(center - support + 0.5f), 0), xmax = std::min((int)(center + support + 0.5f), in);
        t[i].min = xmin; t[i].n = xmax - xmin; float total = 0;
        for (int j = 0; j < t[i].n; j++) { const float w = cubic((j + xmin - center + 0.5f) * inv); t[i].w.push_back(w); total += w; }
        for (auto& w : t[i].w) w /= total;
    }
    return t;
}
}  // namespace

void dinoPixels(const std::vector<uint8_t>& rgb, int w, int h, bool stretch, std::vector<float>& out) {
    const int S = DinoEncoder::kSize; std::vector<float> src; int sw = w, sh = h;
    if (stretch || w == h) { src.resize(rgb.size()); for (size_t i = 0; i < rgb.size(); i++) src[i] = rgb[i] / 255.0f; }
    else {                                                                                // pad with white to a square, the picture in the middle
        const int s = std::max(w, h); sw = sh = s; src.assign((size_t)s * s * 3, 1.0f); const int ox = (s - w) / 2, oy = (s - h) / 2;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) for (int c = 0; c < 3; c++) src[((size_t)(y + oy) * s + x + ox) * 3 + c] = rgb[((size_t)y * w + x) * 3 + c] / 255.0f;
    }
    std::vector<float> tmp, dst((size_t)S * S * 3);
    if (sw == S && sh == S) dst = src;
    else {
        const auto tx = taps(sw, S), ty = taps(sh, S); tmp.assign((size_t)sh * S * 3, 0.0f);
        for (int y = 0; y < sh; y++) for (int x = 0; x < S; x++) for (int c = 0; c < 3; c++) { float a = 0; for (int j = 0; j < tx[x].n; j++) a += tx[x].w[j] * src[((size_t)y * sw + tx[x].min + j) * 3 + c]; tmp[((size_t)y * S + x) * 3 + c] = a; }
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) for (int c = 0; c < 3; c++) { float a = 0; for (int j = 0; j < ty[y].n; j++) a += ty[y].w[j] * tmp[((size_t)(ty[y].min + j) * S + x) * 3 + c]; dst[((size_t)y * S + x) * 3 + c] = a; }
    }
    static const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    out.resize((size_t)3 * S * S);
    for (int c = 0; c < 3; c++) for (int i = 0; i < S * S; i++) { float v = std::min(255.0f, std::max(0.0f, 255.0f * dst[(size_t)i * 3 + c])); v = std::nearbyint(v) / 255.0f; out[(size_t)c * S * S + i] = (v - mean[c]) / sd[c]; }
}

void h3dSigmas(int steps, std::vector<float>& s) {
    s.clear(); for (int i = 0; i < steps; i++) s.push_back(steps > 1 ? 1.0f + (0.001f - 1.0f) * (float)i / (float)(steps - 1) : 1.0f); s.push_back(0.0f);
}

// ------------------------------------------------------------------ the whole pipeline
bool imageToMesh(const std::string& imagePath, const H3DParams& p, Mesh& out, const H3DProgress& progress, std::string& err) {
    auto report = [&](const std::string& s, float f) { return !progress || progress(s, f); };
    auto cancelled = [&]() { err = "cancelled"; return false; };
    std::string ck = p.checkpoint; if (ck.empty()) { err = "no model file given"; return false; }
    if (!Backend::get().init("auto", err)) return false;
    // 1. the picture
    if (!report("Reading the picture", 0.0f)) return cancelled();
    int w, h; std::vector<uint8_t> rgb; if (!loadRgbImage(imagePath, w, h, rgb, err)) return false;
    std::vector<float> px, ctx; dinoPixels(rgb, w, h, p.stretch, px);
    if (!report("Loading the image encoder", 0.02f)) return cancelled();
    { DinoEncoder enc; if (!enc.load(ck, err)) return false; if (!report("Encoding the picture", 0.05f)) return cancelled(); if (!enc.encode(px.data(), ctx, err)) return false; }
    // 2. the flow-matching sampler with classifier-free guidance
    if (!report("Loading the shape network", 0.08f)) return cancelled();
    const int T = Hunyuan3DiT::kLatentTokens, C = Hunyuan3DiT::kChannels; std::vector<float> x((size_t)T * C);
    if (!p.noiseFile.empty()) { std::string d; if (!sys::readFile(p.noiseFile, d) || d.size() != x.size() * 4) { err = "bad noise file"; return false; } memcpy(x.data(), d.data(), d.size()); }
    else { std::mt19937 rng(p.seed); std::normal_distribution<float> nd(0.0f, 1.0f); for (auto& v : x) v = nd(rng); }
    {
        Hunyuan3DiT dit; if (!dit.load(ck, err)) return false;
        std::vector<float> sig, zero(ctx.size(), 0.0f), vc, vu; h3dSigmas(p.steps, sig);
        for (int i = 0; i < p.steps; i++) {
            if (!report("Shaping, step " + std::to_string(i + 1) + " of " + std::to_string(p.steps), 0.10f + 0.70f * (float)i / (float)p.steps)) return cancelled();
            if (!dit.forward(x.data(), sig[i], ctx.data(), DinoEncoder::kTokens, vc, err) || !dit.forward(x.data(), sig[i], zero.data(), DinoEncoder::kTokens, vu, err)) return false;
            const float dt = sig[i + 1] - sig[i];
            for (size_t k = 0; k < x.size(); k++) x[k] += dt * (vu[k] + p.cfg * (vc[k] - vu[k]));      // Euler step of the flow; CFG on the velocity
        }
    }
    const float kScale = 0.9990943042622529f; for (auto& v : x) v /= kScale;                           // the latent format's scale factor
    if (!p.dumpDir.empty()) sys::writeFile(p.dumpDir + "\\final_latent.bin", std::string((const char*)x.data(), x.size() * 4));
    // 3. the shape decoder: a distance volume, then the surface
    if (!report("Loading the shape decoder", 0.80f)) return cancelled();
    std::vector<float> grid; const int res = p.resolution, n = res + 1; const float bound = 1.01f;
    { ShapeVAE vae; if (!vae.load(ck, err) || !vae.setLatents(x.data(), err)) return false;
      if (!vae.decodeGrid(res, bound, 2048, grid, err, [&](float f) { return report("Decoding the volume " + std::to_string((int)(f * 100)) + " %", 0.82f + 0.15f * f); })) return false; }
    if (!p.dumpDir.empty()) sys::writeFile(p.dumpDir + "\\grid.bin", std::string((const char*)grid.data(), grid.size() * 4));
    if (!report("Extracting the surface", 0.97f)) return cancelled();
    CurtainField F; F.n[0] = F.n[1] = F.n[2] = n; F.origin = Vec3(-bound, -bound, -bound); F.h = 2 * bound / res; F.g.resize(grid.size());
    for (int ix = 0; ix < n; ix++) for (int iy = 0; iy < n; iy++) for (int iz = 0; iz < n; iz++) F.g[((size_t)iz * n + iy) * n + ix] = p.threshold - grid[((size_t)ix * n + iy) * n + iz];     // inside = above the threshold
    auto m = Mesh(); extractSurface(F, m.v, m.idx);
    if (m.idx.empty()) { err = "The network produced no surface (try another picture or a lower threshold)."; return false; }
    for (auto& v : m.v) v = Vec3(v.x, -v.z, v.y);                      // the network works Y-up (like glTF); this program is Z-up, the front of the picture looks towards -Y
    out = std::move(m); report("Done", 1.0f); return true;
}
