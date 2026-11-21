// Renders the Doppler's 3D Curtain icon (an object with a yellow curtain wrapped around it) into res/app.ico.
// Same method as the SFX-Parser icon: 6x6 supersampling, sizes 16..256.
// Build: g++ -O2 tools/makeicon.cpp -o build/makeicon.exe && build/makeicon.exe res/app.ico
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace std;

struct C { float r, g, b, a; };
static C over(C d, C s) { float a = s.a + d.a * (1 - s.a); if (a <= 0) return {0,0,0,0};
    return {(s.r*s.a + d.r*d.a*(1-s.a))/a, (s.g*s.a + d.g*d.a*(1-s.a))/a, (s.b*s.a + d.b*d.a*(1-s.a))/a, a}; }
static bool inRR(float x, float y, float x0, float y0, float x1, float y1, float r) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    float cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x), cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
    return (x-cx)*(x-cx) + (y-cy)*(y-cy) <= r*r;
}

static C shade(float u, float v) {
    if (!inRR(u, v, 0.02f, 0.02f, 0.98f, 0.98f, 0.20f)) return {0,0,0,0};
    C c = {0.08f + 0.06f*v, 0.10f + 0.10f*v, 0.17f + 0.20f*v, 1};                       // dark navy gradient
    const float cx = 0.5f, cy = 0.53f, dx = u - cx, dy = v - cy, r = sqrtf(dx*dx + dy*dy), th = atan2f(dy, dx);
    const float bodyR = 0.21f + 0.025f * sinf(3 * th + 1.0f) + 0.012f * sinf(7 * th), wrapR = 0.35f + 0.02f * sinf(3 * th + 1.0f) + 0.008f * sinf(7 * th);
    if (r < wrapR) c = over(c, {1.0f, 0.82f, 0.18f, 0.20f});                            // the curtain (yellow, translucent)
    if (r < wrapR && r > bodyR) {                                                      // the mesh of the curtain: rings and spokes
        const float ring = fabsf(fmodf(r - bodyR, (wrapR - bodyR) / 3.0f)), spoke = fabsf(fmodf(th + 3.14159f, 6.28318f / 16.0f) - 6.28318f / 32.0f) * r;
        if (spoke < 0.0045f || ring < 0.0045f) c = over(c, {1.0f, 0.85f, 0.25f, 0.35f});
    }
    if (fabsf(r - wrapR) < 0.013f) c = {1.0f, 0.84f, 0.22f, 1};                         // the wrap edge
    if (r < bodyR) {                                                                   // the object: a shaded body
        const float l = 0.55f + 0.45f * fmaxf(0.0f, (-dx * 0.6f - dy * 0.8f) / bodyR);
        c = {0.50f * l + 0.10f, 0.58f * l + 0.10f, 0.76f * l + 0.10f, 1};
        static const float vx[] = {-0.10f, 0.02f, 0.11f, -0.03f, 0.08f, -0.13f, 0.14f}, vy[] = {-0.06f, 0.10f, -0.02f, -0.12f, 0.12f, 0.05f, 0.08f};
        for (int i = 0; i < 7; i++) if ((dx - vx[i]) * (dx - vx[i]) + (dy - vy[i]) * (dy - vy[i]) < 0.016f * 0.016f) c = {0.20f, 0.45f, 1.0f, 1};   // vertices are blue
    }
    return c;
}

int main(int argc, char** argv) {
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256}, N = 7;
    vector<vector<uint8_t>> img(N);
    for (int k = 0; k < N; k++) {
        int s = sizes[k]; const int SS = 6;
        vector<uint8_t>& b = img[k];
        b.resize(40 + s*s*4 + s*((s+31)/32)*4);
        uint32_t* hd = (uint32_t*)b.data();
        hd[0] = 40; hd[1] = s; hd[2] = s*2; ((uint16_t*)b.data())[6] = 1; ((uint16_t*)b.data())[7] = 32; hd[4] = 0; hd[5] = s*s*4;
        uint8_t* px = b.data() + 40;
        for (int y = 0; y < s; y++) for (int x = 0; x < s; x++) {
            float r = 0, g = 0, bl = 0, a = 0;
            for (int j = 0; j < SS; j++) for (int i = 0; i < SS; i++) {
                C c = shade((x + (i + .5f)/SS)/s, (y + (j + .5f)/SS)/s);
                r += c.r*c.a; g += c.g*c.a; bl += c.b*c.a; a += c.a;
            }
            float inv = 1.f/(SS*SS); uint8_t* o = px + ((s-1-y)*s + x)*4;
            if (a > 0) { o[0] = (uint8_t)(bl/a*255+.5f); o[1] = (uint8_t)(g/a*255+.5f); o[2] = (uint8_t)(r/a*255+.5f); }
            o[3] = (uint8_t)(a*inv*255+.5f);
        }
    }
    FILE* f = fopen(argc > 1 ? argv[1] : "app.ico", "wb");
    if (!f) return 1;
    uint16_t hdr[3] = {0, 1, (uint16_t)N}; fwrite(hdr, 2, 3, f);
    uint32_t off = 6 + 16*N;
    for (int k = 0; k < N; k++) {
        int s = sizes[k]; uint8_t e[16] = {(uint8_t)(s==256?0:s), (uint8_t)(s==256?0:s), 0, 0, 1, 0, 32, 0};
        *(uint32_t*)(e+8) = (uint32_t)img[k].size(); *(uint32_t*)(e+12) = off; off += (uint32_t)img[k].size(); fwrite(e, 1, 16, f);
    }
    for (auto& b : img) fwrite(b.data(), 1, b.size(), f);
    fclose(f); return 0;
}
