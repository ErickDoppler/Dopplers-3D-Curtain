cmake_minimum_required(VERSION 3.20)
project(Dopplers3DCurtain CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# the shared core: meshes and file formats, the curtain, the project file
set(CORE src/mesh.cpp src/curtain.cpp src/doc.cpp src/project.cpp src/sys.cpp)

# the window
set(GUI src/main.cpp src/view.cpp src/toolbar.cpp src/download.cpp)
add_executable(Dopplers3DCurtain WIN32 ${CORE} ${GUI} res/app.rc)
target_include_directories(Dopplers3DCurtain PRIVATE src)
target_compile_definitions(Dopplers3DCurtain PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Dopplers3DCurtain PRIVATE opengl32 comctl32 comdlg32 gdi32 user32 shell32 ole32 windowscodecs winhttp uuid -municode -static-libgcc -static-libstdc++)

# the command line (console program, for scripts and Claude Code)
add_executable(d3c ${CORE} src/cli.cpp)
target_include_directories(d3c PRIVATE src)
target_compile_definitions(d3c PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(d3c PRIVATE gdi32 user32 shell32 -static-libgcc -static-libstdc++)
