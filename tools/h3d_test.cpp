// Checks the Hunyuan3D port stage by stage against the tensors dumped by tools/h3d_ref.py (ComfyUI's own pipeline).
//   h3d_test dino | dit | vae        (D3C_BACKEND=cpu|gpu|auto; D3C_REF = reference folder)
// Pass criterion for every stage: relative L2 error against the reference below the stated limit (the weights are 16-bit).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "h3d/dino.h"
#include "h3d/dit.h"
#include "h3d/vae.h"
#include "h3d/pipeline.h"
#include "sys.h"

static std::string refDir() { const char* e = getenv("D3C_REF"); return e ? e : (std::string(getenv("TEMP")) + "\\h3d_ref"); }
static bool readBin(const std::string& n, std::vector<float>& v) {
    std::string d; if (!sys::readFile(refDir() + "\\" + n + ".bin", d)) return false; v.resize(d.size() / 4); memcpy(v.data(), d.data(), v.size() * 4); return true; }
static std::string ckpt() { const char* e = getenv("D3C_CKPT"); return e ? e : "C:\\workenv\\ComfyUI-Easy-Install\\ComfyUI-Easy-Install\\ComfyUI\\models\\checkpoints\\hunyuan3d-dit-v2_fp16.safetensors"; }
static int fails = 0;
static void compare(const char* what, const std::vector<float>& a, const std::vector<float>& ref, double limit) {
    if (a.size() != ref.size()) { printf("FAIL: %s size %zu vs %zu\n", what, a.size(), ref.size()); fails++; return; }
    double d2 = 0, r2 = 0, mx = 0; size_t nan = 0;
    for (size_t i = 0; i < a.size(); i++) { if (!std::isfinite(a[i])) { nan++; continue; } double d = a[i] - ref[i]; d2 += d * d; r2 += (double)ref[i] * ref[i]; mx = std::max(mx, std::fabs(d)); }
    const double rel = std::sqrt(d2 / std::max(r2, 1e-30)); const bool ok = rel < limit && nan == 0;
    printf("%s %s: relative L2 error %.2e (limit %.0e), max abs %.4f, non-finite %zu\n", ok ? "ok:  " : "FAIL:", what, rel, limit, mx, nan); if (!ok) fails++;
}

int main(int argc, char** argv) {
    const std::string stage = argc > 1 ? argv[1] : "dino"; std::string err;
    if (!Backend::get().init("auto", err)) { printf("backend: %s\n", err.c_str()); return 2; }
    printf("backend: %s\n", Backend::get().name.c_str());
    auto t0 = std::chrono::steady_clock::now(); auto lap = [&](const char* w) { double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); printf("  %s: %.1f s\n", w, s); t0 = std::chrono::steady_clock::now(); };
    if (stage == "dino") {
        DinoEncoder enc; if (!enc.load(ckpt(), err)) { printf("load: %s\n", err.c_str()); return 2; } lap("load");
        std::vector<float> px, ref, out; if (!readBin("pixel_values", px) || !readBin("dino_last", ref)) { printf("reference tensors missing (run tools/h3d_ref.py)\n"); return 2; }
        if (!enc.encode(px.data(), out, err)) { printf("encode: %s\n", err.c_str()); return 2; } lap("encode");
        compare("DINOv2 last_hidden_state", out, ref, 3e-3);
    }
    if (stage == "dit") {
        Hunyuan3DiT dit; if (!dit.load(ckpt(), err)) { printf("load: %s\n", err.c_str()); return 2; } lap("load");
        std::vector<float> noise, ctx, r1, r2, r3, out; if (!readBin("noise", noise) || !readBin("dino_last", ctx) || !readBin("dit_cond_s1", r1) || !readBin("dit_uncond_s1", r2) || !readBin("dit_cond_s05", r3)) { printf("reference tensors missing\n"); return 2; }
        if (!dit.forward(noise.data(), 1.0f, ctx.data(), 1370, out, err)) { printf("forward: %s\n", err.c_str()); return 2; } lap("forward (cond, sigma 1)"); compare("DiT conditional, sigma 1.0", out, r1, 4e-3);
        std::vector<float> zero(ctx.size(), 0.0f);
        if (!dit.forward(noise.data(), 1.0f, zero.data(), 1370, out, err)) { printf("forward: %s\n", err.c_str()); return 2; } lap("forward (uncond)"); compare("DiT unconditional, sigma 1.0", out, r2, 4e-3);
        if (!dit.forward(noise.data(), 0.5f, ctx.data(), 1370, out, err)) { printf("forward: %s\n", err.c_str()); return 2; } lap("forward (cond, sigma 0.5)"); compare("DiT conditional, sigma 0.5", out, r3, 4e-3);
        { std::vector<float> a1, a2; if (readBin("dit_cond_s0334", a1) && readBin("dit_cond_s0001", a2)) {
            if (dit.forward(noise.data(), 0.334f, ctx.data(), 1370, out, err)) compare("DiT conditional, sigma 0.334", out, a1, 4e-3);
            if (dit.forward(noise.data(), 0.001f, ctx.data(), 1370, out, err)) compare("DiT conditional, sigma 0.001", out, a2, 4e-3); } }
    }
    if (stage == "vae") {
        ShapeVAE vae; if (!vae.load(ckpt(), err)) { printf("load: %s\n", err.c_str()); return 2; } lap("load");
        std::vector<float> noise, rt, rg, grid; if (!readBin("noise", noise) || !readBin("vae_transformer", rt) || !readBin("vae_grid32", rg)) { printf("reference tensors missing\n"); return 2; }
        if (!vae.setLatents(noise.data(), err)) { printf("setLatents: %s\n", err.c_str()); return 2; } lap("transformer + keys/values"); compare("VAE transformer output", vae.transformerOut, rt, 4e-3);
        const int res = 32, n = res + 1;
        if (!vae.decodeGrid(res, 1.01f, 2048, grid, err)) { printf("decodeGrid: %s\n", err.c_str()); return 2; } lap("grid 33^3");
        std::vector<float> mine((size_t)n * n * n);                                           // ComfyUI returns the grid with its last two axes swapped: [ix][iz][iy]
        for (int ix = 0; ix < n; ix++) for (int iy = 0; ix < n && iy < n; iy++) for (int iz = 0; iz < n; iz++) mine[((size_t)ix * n + iz) * n + iy] = grid[((size_t)ix * n + iy) * n + iz];
        compare("VAE signed distance grid 33^3", mine, rg, 4e-3);
    }
    if (stage == "pre") {                                                                  // the picture preprocessing against ComfyUI's pixel_values (same file, alpha ignored, squashed to a square)
        int w, h; std::vector<uint8_t> rgb;
        _putenv("D3C_IGNORE_ALPHA=1");
        const std::string img = getenv("D3C_IMAGE") ? getenv("D3C_IMAGE") : "C:\\workenv\\ComfyUI-Easy-Install\\ComfyUI-Easy-Install\\ComfyUI\\input\\BallerinaBunny.png";
        std::vector<float> px, ref; if (!loadRgbImage(img, w, h, rgb, err) || !readBin("pixel_values", ref)) { printf("pre: %s\n", err.c_str()); return 2; }
        dinoPixels(rgb, w, h, true, px); compare("image preprocessing (bicubic antialias, normalised)", px, ref, 2e-3);
    }
    if (stage == "e2e") {                                                                  // the whole sampler with ComfyUI's own noise, a few steps, against ComfyUI's latent and distance grid
        _putenv("D3C_IGNORE_ALPHA=1");
        std::vector<float> rl, rg; if (!readBin("e2e_latent", rl) || !readBin("e2e_grid64", rg)) { printf("run tools/h3d_ref.py with H3D_STEPS set first\n"); return 2; }
        H3DParams p; p.checkpoint = ckpt(); p.steps = atoi(getenv("H3D_STEPS") ? getenv("H3D_STEPS") : "4"); p.stretch = true; p.resolution = 64; p.noiseFile = refDir() + "\\noise.bin"; p.dumpDir = refDir();
        const std::string img = getenv("D3C_IMAGE") ? getenv("D3C_IMAGE") : "C:\\workenv\\ComfyUI-Easy-Install\\ComfyUI-Easy-Install\\ComfyUI\\input\\BallerinaBunny.png";
        Mesh m; auto prog = [&](const std::string& s, float f) { printf("  %5.1f%%  %s\n", f * 100, s.c_str()); fflush(stdout); return true; };
        if (!imageToMesh(img, p, m, prog, err)) { printf("pipeline: %s\n", err.c_str()); return 2; } lap("pipeline");
        std::vector<float> lat, grid; readBin("final_latent", lat); readBin("grid", grid);
        compare("final latent after sampling", lat, rl, 2e-2);
        const int n = 65; std::vector<float> mine((size_t)n * n * n);
        for (int ix = 0; ix < n; ix++) for (int iy = 0; iy < n; iy++) for (int iz = 0; iz < n; iz++) mine[((size_t)ix * n + iz) * n + iy] = grid[((size_t)ix * n + iy) * n + iz];
        compare("distance grid 65^3 from the sampled latent", mine, rg, 5e-2);
        printf("  mesh: %zu vertices, %zu triangles\n", m.v.size(), m.triCount());
    }
    printf("\n%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
