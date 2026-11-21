#pragma once
#include <string>
#include <windows.h>

bool cudaInstalled();                       // ggml-cuda.dll, cudart64_13.dll, cublas64_13.dll, cublasLt64_13.dll next to the exe
bool hunyuanInstalled();                    // models\hunyuan3d\hunyuan3d-dit-v2_fp16.safetensors complete
std::string hunyuanModelPath();
void showSetupDialog(HWND parent, bool first);      // modal: CUDA libraries and the Hunyuan3D model, with progress, cancel and resume
