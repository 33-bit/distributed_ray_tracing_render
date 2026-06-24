# Mesh assets

`dragon.stl` — downloaded from [free3d.com/3d-models/dragon](https://free3d.com/3d-models/dragon)
(binary STL, 37,986 triangles, "Exported from Blender-2.74"). Used here for an
internal/academic demo of the mesh-import feature (`scene/stl_loader.hpp`,
`"type": "mesh"` in scene JSON).

`castle/castle.obj` — text OBJ, 19,850 vertices / 16,473 faces, "Exported from
DAZ Studio 3.1", multiple `usemtl` groups (`default`, `default_doors`,
`default_floor`, `default_roof`, `default_windows`). Used to demo OBJ import +
per-group materials (`scene/obj_loader.hpp`, the `material_map` field on a
`"type": "mesh"` object). Original textures (`Maps/*.jpg`, referenced by the
matching `.mtl`) are **not** included — this renderer has no texture mapping,
so groups are recolored with flat scene materials instead.

Both downloaded from free model sites that require an account and whose
license terms vary per model — **verify the specific model's license before
any public redistribution or commercial use**; this repo includes them on the
assumption of coursework/personal use only.
