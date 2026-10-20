# Making a voxel model in ten minutes

This guide walks through replacing one object in the 3D view with your own model. The format
reference is in [VOXEL_TILESETS.md](VOXEL_TILESETS.md).

## 1. Get the tools

* Install [MagicaVoxel](https://ephtracy.github.io/), a free voxel editor for Windows and macOS.
  On Linux it runs under Wine. [Goxel](https://goxel.xyz/) also saves `.vox` files.
* Run Cataclysm 3D once so the `gfx/voxel` folder exists.

## 2. Start your own set

Copy `gfx/voxel/example` to `gfx/voxel/my_set` and change the name in
`gfx/voxel/my_set/voxelset.json`:

```json
{
  "name": "My voxel set",
  "voxels_per_tile": 8,
  "models": []
}
```

In the game open Settings, Graphics, Tileset options, set `3D voxel tileset` to `My voxel set`.

## 3. Pick what to replace

Look at the object in game with the look command (`x`) and note its name, then find its id:

* Terrain and furniture ids start with `t_` and `f_`, for example `t_wall_w` or `f_sofa`.
* Monster ids start with `mon_`, for example `mon_zombie_fat`.
* Item ids have no prefix, for example `fire_axe`.
* Vehicle parts use `vp_` and the part id, for example `vp_frame`.

The ids are in the game data under `data/json`. Searching that folder for the name shown in game
finds the id.

You can also replace a whole family of objects at once with a built-in type, such as every tree
with `"builtin": "tree"` or every gun lying on the ground with `"builtin": "item_gun"`.

## 4. Model it

The easiest start is an existing model. `gfx/voxel/starter/builtin` holds every built-in model as
a `.vox` file, for example `zombie.vox`, `tree.vox` or `wall_brick.vox`. Open one in MagicaVoxel,
change it and save it into your set, for example `my_set/monsters/fat_zombie.vox`.

Keep these rules in mind:

* **Scale.** One map tile is 8 by 8 voxels. One floor of a building is 16 voxels high. A person is
  about 13 voxels tall.
* **Ground.** The bottom layer of the model touches the floor.
* **Centering.** The model is centered on its tile. Wider models such as trees spill over the
  neighbouring tiles evenly, so keep the width even.
* **Front.** Creatures and vehicles face MagicaVoxel `+y`, which is north in game. They turn
  toward where they walk.
* **Glass.** Give a palette color the glass material in MagicaVoxel to make it translucent, like
  windows.
* **Size limit.** Up to 256 voxels in each direction. Small models are faster.

### Working in Blender or another 3D tool

You can skip voxel editors entirely. Export the model as Wavefront `.obj` with its `.mtl` and
texture next to it, and point `file` at the `.obj`: the game voxelizes it when it loads. Build in
meters, one meter per tile, facing `-Z` with `+Y` up. Keep shapes chunky: details smaller than an
eighth of a meter disappear at 8 voxels per tile. For an animation, export one `.obj` per key frame
and list them under `animations`.

## 5. Register it

Add an entry to the `models` list:

```json
{ "id": "mon_zombie_fat", "file": "monsters/fat_zombie.vox" }
```

Useful extras:

* `"files": [ "a.vox", "b.vox" ]` picks a variant per tile, good for trees and rocks.
* `"random_rotation": true` turns static objects randomly so repeats look less uniform.
* `"tint": true` multiplies your model by the object's game color. Model the object in light
  grey and one file can serve every color of sofa.
* `"offset": [ 0, 2, 0 ]` lifts or shifts the model by whole voxels.
* `"emissive": true` makes it glow in the dark.

Objects whose id you did not list fall back to their `looks_like` relatives from the game data,
so a model for `t_tree` also covers every tree that looks like it.

## 6. Check it in game

Switch `3D voxel tileset` to `Built-in procedural models` and back to your set to reload changed
files without restarting. If a model does not show up, look for lines with `voxel` in
`config/debug.log`.

## 7. Share it

Open an issue with the voxel models template on the project page and attach your set as a zip
with a screenshot.
