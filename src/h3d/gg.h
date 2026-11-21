// ggml helpers: the backend (GPU when the CUDA libraries are there, else CPU), model weights from a safetensors file, and one-shot graphs.
#pragma once
#include <string>
#include <vector>
#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "h3d/st.h"

struct Backend {
    ggml_backend_t main = nullptr, cpu = nullptr; bool gpu = false; std::string name;
    static Backend& get();
    bool init(const std::string& want, std::string& err);       // "auto" (GPU if available), "cpu", "gpu"; also the D3C_BACKEND environment variable
    void sync() { if (main) ggml_backend_synchronize(main); }
};

// Tensors of a safetensors file, copied into a backend buffer. 2-D and 4-D "*.weight" become F16, everything else F32.
class Weights {
public:
    ~Weights() { release(); }
    bool open(const std::string& path, std::string& err);
    ggml_tensor* want(const std::string& name);                  // before upload(); nullptr (and an error) when missing
    bool upload(std::string& err);
    void release();
    const std::string& error() const { return missing_; }
    StFile file;
private:
    ggml_context* ctx_ = nullptr; ggml_backend_buffer_t buf_ = nullptr;
    std::vector<std::pair<ggml_tensor*, const StTensor*>> pending_; std::string missing_;
};

// A graph that is built, allocated, filled with inputs, computed once, and read back.
class Graph {
public:
    explicit Graph(int maxNodes);
    ~Graph();
    ggml_context* ctx = nullptr; ggml_cgraph* g = nullptr;
    ggml_tensor* input(ggml_type t, int64_t a, int64_t b = 1, int64_t c = 1, int64_t d = 1);
    void output(ggml_tensor* t) { ggml_set_output(t); ggml_build_forward_expand(g, t); }
    bool alloc(std::string& err);
    bool run(std::string& err);
    static void set(ggml_tensor* t, const void* data, size_t bytes) { ggml_backend_tensor_set(t, data, 0, bytes); }
    static void get(const ggml_tensor* t, void* data, size_t bytes) { ggml_backend_tensor_get(t, data, 0, bytes); }
private:
    ggml_backend_sched_t sched_ = nullptr; std::vector<uint8_t> meta_;
};

// layer norm over the first dimension with learned scale and bias (either may be null)
ggml_tensor* layerNorm(ggml_context* c, ggml_tensor* x, ggml_tensor* w, ggml_tensor* b, float eps);
ggml_tensor* linear(ggml_context* c, ggml_tensor* w, ggml_tensor* b, ggml_tensor* x);       // w [in,out], x [in,tokens]
// scaled dot product attention, q/k/v as [head_dim, heads, tokens] (the usual ggml layout of a projected sequence); returns [head_dim*heads, tokens_q]
ggml_tensor* attention(ggml_context* c, ggml_tensor* q, ggml_tensor* k, ggml_tensor* v, float scale);
