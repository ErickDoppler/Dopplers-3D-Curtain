# d3c - the command line

`d3c.exe` (built by `build.bat` into `dist\`) does what the window does, without a window. It prints plain text, or with `--json` (anywhere on the line) **one JSON object**, so a script or Claude Code can read the result. Exit code `0` = success, `1` = error (message on stderr, or `{"ok": false, "error": ...}` with `--json`), `2` = unknown command, `3` = reserved.

Units are the model's own. **Z is up.** Every model is put on the plate: centred in x and y, lowest point at z = 0. glTF / GLB files (Y-up by definition) are turned to Z-up on load.

## Commands

```
d3c info <model>                      vertices, triangles, size, open edges, watertight or not
d3c curtain <model | project.curtain> [options]     compute the curtain, write files
d3c reduce <model> --quality <percent> --out <file.obj|.glb>     "Object details": keep that share of the vertices
d3c convert <in> <out>                OBJ / STL / PLY / glTF / GLB in, OBJ / GLB out (identical vertices are welded)
d3c eval <file.cvf> <x> <y> <z> ...   signed distance of the curtain at points (negative = inside)
d3c image2mesh <picture> --out <file.obj|.glb> [options]     picture to 3D with the Hunyuan3D 2.0 shape network (model and CUDA libraries needed)
```

### `d3c curtain` options

| Option | Meaning | Default |
|---|---|---|
| `--mode smooth\|hull` | `smooth` = offset that becomes a sphere as the distance grows; `hull` = convex hull plus distance | smooth |
| `--distance <percent>` | distance of the curtain, percent of the object's bounding-box diagonal (0 - 300) | 5 |
| `--precision <16..384>` | **fidelity**: grid cells along the longest side. High follows every curve, low cuts the corners. Time and memory grow with its cube (384 = about 57 million cells: 11 s and 0.65 GB for a 337 000-vertex brain) | 128 |
| `--density <200..2000000>` | **vertices of the wrap mesh**, independent of the precision | 30000 |
| `--scale <pct>` or `<x,y,z>` | object size in percent per axis, before the curtain is computed | 100 |
| `--rotate <x,y,z>` | rotation in degrees about the world axes (Z-up), before the curtain is computed | 0,0,0 |
| `--out <file.obj\|.glb>` | the wrap surface | |
| `--object-out <file.obj\|.glb>` | the object after scale and rotation, on the plate | |
| `--header <file.h> [--name id]` | engine header: arrays plus `static inline float <name>_eval(float x, float y, float z)` | name = `curtain_<file>` |
| `--cvf <file.cvf>` | the distance field as a small binary (`CVF1`, 16-bit values) | |
| `--project <file.curtain>` | a project file (opens in the window) | |

A `.curtain` project can be given instead of a model; options then override what it holds.

### `d3c image2mesh` options

| Option | Meaning | Default |
|---|---|---|
| `--out <file.obj\|.glb>` | the model (Z-up, upright, centred around the origin) | required |
| `--seed <n>` | random start of the shape | 0 |
| `--steps <1..200>` | sampler steps | 20 |
| `--cfg <x>` | classifier-free guidance | 8 |
| `--resolution <16..512>` | voxel resolution of the distance volume | 256 |
| `--threshold <x>` | surface level in the volume | 0.7 |
| `--model <file>` | `hunyuan3d-dit-v2_fp16.safetensors` | `models\hunyuan3d\` next to the program |
| `--backend auto\|cpu\|gpu` | where to run | auto (CUDA when the libraries are there) |
| `--stretch 1` | squash the picture to a square like the ComfyUI workflow, instead of padding with white | off |

Time on an RTX 3080 Ti: about 30 s for the 20 steps, plus the volume decoding (resolution 128: about 25 s). On the processor it is 40 times slower. Combine it with `curtain` on the result: `d3c curtain model.glb ...`.

### Example

```
d3c curtain car.glb --mode smooth --distance 4 --precision 256 --density 40000 ^
     --out car_wrap.obj --header car_wrap.h --name car_wrap --cvf car_wrap.cvf --project car.curtain --json
```

returns, for example:

```json
{ "ok": true, "mode": "smooth", "distance_percent": 4, "precision": 256, "density": 40000,
  "grid": [257, 241, 129], "cell": 0.0123, "level": 0.19,
  "wrap_vertices": 40000, "wrap_triangles": 80000, "wrap_open_edges": 0, "seconds": 5.2,
  "object_vertices": 336670, "object_size": [4.1, 1.8, 1.4],
  "files": [ { "what": "wrap surface", "path": "car_wrap.obj" }, ... ] }
```

`wrap_open_edges` is 0 for a closed wrap. `d3c eval car_wrap.cvf 0 0 0.7 --json` then answers whether a point is inside the curtain.

## The engine side

The header contains the grid (`NAME_NX/NY/NZ`, `NAME_ORIGIN`, `NAME_CELL`) and 16-bit values. `NAME_eval(x, y, z)` does one trilinear lookup and returns the signed distance to the curtain surface in the model's units: negative inside, zero on the curtain, growing with the distance outside the grid box. The values are clamped to +-16 cells around the surface, which is all a collision or a "wrap" test needs.

## For Claude Code

* Prefer `--json`; read `ok`, and `error` when it is false.
* Start with `info`: `watertight`, `open_edges` and the size tell if the model is clean and what scale it has. The distance is relative to the size, so no unit conversion is needed.
* Precision and density are independent: raise **precision** if the wrap cuts corners that matter, raise **density** if the mesh is too coarse; neither changes the other.
* Large precisions are slower (384: about 11 s and 0.65 GB for a 337 000-vertex model); try 128 first.
