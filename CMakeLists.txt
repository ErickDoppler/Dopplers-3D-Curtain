cmake_minimum_required(VERSION 3.20)
project(Dopplers3DCurtain CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# the shared core: meshes and file formats, the curtain, the project file
set(CORE src/mesh.cpp src/curtain.cpp src/doc.cpp src/project.cpp src/sys.cpp)

# Hunyuan3D 2.0 (image to 3D) on ggml. The runtime DLLs (ggml, the CPU variants, CUDA) sit next to the programs (runtime\ is copied by build.bat);
# the CUDA ones come from the download window / the llama.cpp release.
set(RT ${CMAKE_SOURCE_DIR}/runtime)
set(H3D src/h3d/st.cpp src/h3d/gg.cpp src/h3d/dino.cpp src/h3d/dit.cpp src/h3d/vae.cpp src/h3d/pipeline.cpp)
set(GGML ${RT}/ggml.dll ${RT}/ggml-base.dll)

# the window
set(GUI src/main.cpp src/view.cpp src/toolbar.cpp src/download.cpp)
add_executable(Dopplers3DCurtain WIN32 ${CORE} ${GUI} ${H3D} res/app.rc)
target_include_directories(Dopplers3DCurtain PRIVATE src third_party/ggml/include)
target_compile_definitions(Dopplers3DCurtain PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Dopplers3DCurtain PRIVATE ${GGML} opengl32 comctl32 comdlg32 gdi32 user32 shell32 ole32 windowscodecs winhttp uuid -municode -static-libgcc -static-libstdc++)

# the command line (console program, for scripts and Claude Code)
add_executable(d3c ${CORE} ${H3D} src/cli.cpp)
target_include_directories(d3c PRIVATE src third_party/ggml/include)
target_compile_definitions(d3c PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(d3c PRIVATE ${GGML} gdi32 user32 shell32 ole32 windowscodecs uuid -static-libgcc -static-libstdc++)

# checks of the port against ComfyUI's tensors (tools/h3d_ref.py)
add_executable(h3d_test tools/h3d_test.cpp ${CORE} ${H3D})
target_include_directories(h3d_test PRIVATE src third_party/ggml/include)
target_compile_definitions(h3d_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(h3d_test PRIVATE ${GGML} gdi32 user32 shell32 ole32 windowscodecs uuid -static-libgcc -static-libstdc++)
