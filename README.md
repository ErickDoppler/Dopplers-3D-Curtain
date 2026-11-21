# Doppler's 3D Curtain

![icon](res/icon-preview.png)

A Windows tool that wraps a **mathematical curtain** around a 3D model, for use in a procedural game engine, and (planned, next stage) turns a **picture into a 3D model**.

The curtain is a smooth shell at a chosen distance from the object that follows its curves. It is kept as a **signed distance field** on a grid: for any point in space, one trilinear lookup (8 reads) tells how far the point is from the curtain (negative inside). That is cheap enough for an engine, and it is what the exports contain.

![toolbar](docs/toolbar.png)

*The tool bar (hover a button for a hint): Open, Save, Save As, Undo, Redo, Curtain distance, Curtain precision, Wrap density, Object size, Rotate, Move, Object details.*

![A model with its curtain in red while editing](docs/screenshot-wrap.png)

*Editing the curtain: it is red and translucent, the vertices of the object are blue.*

![Object size handles, like in Cura](docs/screenshot-scale.png) ![Wrap only](docs/screenshot-wrap-only.png)

## Curtain settings

| Setting | What it does |
|---|---|
| **Distance** | How far the curtain is from the object (percent of the object size). The further, the closer to a clean sphere ("Smooth - sphere limit"), or to the object's convex hull plus the distance ("Convex hull - distance"). |
| **Precision** | The *fidelity* of the wrap: grid cells along the longest side (16 - 384). High precision follows every curve like a vinyl wrap on a car; low precision cuts the corners on the turns. It does **not** change how many vertices the wrap has. |
| **Wrap density** | The number of vertices of the wrap mesh (200 - 2 000 000), independent of the precision. Fewer: surface-preserving edge collapse (the wrap stays closed). More: the triangles are subdivided and the new vertices are moved onto the same surface. |
| **Hide object** | Leaves only the wrap in the 3D view. |

Colours: object vertices are blue, the curtain is red while you edit it and yellow in the view mode (button **Curtain**, with a transparency slider).

## 3D view

The controls follow Ultimaker Cura: right mouse button rotates and tilts, middle button (or Shift + left) pans, the wheel zooms, left drag on the object moves it on the plate. The vertical stack on the left switches the view: Center, Left, Right, Front, Back, Top, Bottom. Tools show Cura-like handles in the view: move arrows, size boxes, rotation rings; the sliders and number boxes sit over the view. Undo and Redo keep 200 steps.

**Object details** removes a share of the object's vertices (0 - 100 %, steps of 0.01 %): a grid of 10000 x 10000 x 10000 cells is laid over the object, the vertices of the cells that "fall out" are merged into their nearest kept neighbour, so the holes close.

## Files

* Open: **OBJ, STL, PLY, glTF / GLB**, `.curtain` projects, and pictures (PNG, JPG, BMP: opened in an Image tab). *STEP / STP is planned; it needs a CAD kernel to turn curved surfaces into triangles, so for now export the part as STL or OBJ.*
* Each file is a tab. Drop files on the window.
* Save: `.curtain` (the mesh, the transform, the curtain settings and the fitted field).
* Export: **engine header** (`.h` with a `<name>_eval(x, y, z)` function), **binary field** (`.cvf`), the wrap surface (OBJ), the object (OBJ / GLB).

## Image to 3D

Image tabs hold the settings of the Hunyuan3D 2.0 workflow (seed, steps, CFG, voxel resolution, surface threshold) and a shared queue. The **engine itself is not built into this version yet**: it is the next stage (a port of the Hunyuan3D shape network that runs without ComfyUI, like the other models in Dopplers Bakery). On the first start a window offers the **CUDA libraries** and the **Hunyuan3D model** (4.9 GB) for download, with progress, cancel and resume.

## Command line

`d3c.exe` is a console program with the same core, made for scripts and for Claude Code. See [docs/CLI.md](docs/CLI.md).

```
d3c info model.glb --json
d3c curtain model.glb --mode smooth --distance 5 --precision 192 --density 20000 --out wrap.obj --header wrap.h --cvf wrap.cvf --project model.curtain --json
```

## Building

Needs the toolchain in `c:\workenv` (w64devkit, ninja, cmake), the same as Dopplers Bakery:

```
build.bat      dist\Dopplers3DCurtain.exe   the window
               dist\d3c.exe                 the command line
```

`tools\core_test.cpp` checks the core without a window; `tools\drv.ps1` drives the window through messages for tests.

## License

Same terms as Dopplers Bakery: **free for personal use; enterprise use requires US$100 per workstation** (see [LICENSE](LICENSE)).
