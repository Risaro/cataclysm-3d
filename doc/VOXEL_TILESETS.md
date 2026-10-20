# Voxel tilesets

New to voxel modeling? Start with [VOXEL_MODELING_GUIDE.md](VOXEL_MODELING_GUIDE.md).

The 3D view draws every object with a voxel model. Without a voxel tileset the game uses built-in
procedural models. A voxel tileset replaces any of them with models made in
[MagicaVoxel](https://ephtracy.github.io/) or any editor that saves `.vox` files.

## Layout

A voxel tileset is a folder inside `gfx/voxel/` (or the user `gfx/voxel/` folder):

```
gfx/voxel/my_set/
    voxelset.json
    walls/brick.vox
    monsters/zombie.vox
```

Select it in Settings, Graphics, Tileset options, `3D voxel tileset`. The list is read at startup.

## voxelset.json

```json
{
  "name": "My voxel set",
  "voxels_per_tile": 8,
  "models": [
    { "id": "t_wall", "file": "walls/plaster.vox" },
    { "id": [ "mon_zombie", "mon_zombie_fat" ], "files": [ "monsters/zombie_a.vox", "monsters/zombie_b.vox" ] },
    { "id": "f_chair", "file": "furniture/chair.vox", "random_rotation": true },
    { "builtin": "tree", "file": "nature/oak.vox" },
    { "builtin": "wall", "material": "brick", "file": "walls/brick.vox" },
    { "builtin": "chair", "file": "furniture/chair_white.vox", "tint": true }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Name shown in the options menu. |
| `voxels_per_tile` | How many voxels make the width of one map tile. One z-level is twice that height. Default 8. |
| `id` | One game id or a list of them: terrain `t_*`, furniture `f_*`, monsters `mon_*`, item ids, vehicle parts `vp_<part id>`, fields `fd_*`, traps `tr_*`, and `player_male`, `player_female`, `npc_male`, `npc_female`, `player`, `npc`. |
| `builtin` | Replaces a built-in model type for every object that uses it, for example `tree`, `wall`, `zombie`, `item_gun`. Use `material` to limit a floor, wall or window override to one material. |
| `file` / `files` | The `.vox` file, or a list of variants picked per tile. Paths are relative to the folder. |
| `offset` | `[x, y, z]` shift in voxels. `y` is up. |
| `scale` | Extra scale factor. |
| `rotates` | `false` keeps the model unrotated even for creatures and vehicles. Default `true`. |
| `random_rotation` | Turns the model by a random multiple of 90 degrees per tile. |
| `tint` | Multiplies the model by the color the game gives the object, so one white model can serve furniture of every color. |
| `emissive` | The model glows and ignores darkness, like fire. |
| `animations` | Frame by frame animations per state, see below. |
| `mesh_height` | For `.obj` files: fit the model to this many voxels of height. |
| `mesh_scale` | For `.obj` files: scale factor on top of one OBJ unit per tile. |

Lookup order for an object: its exact `id`, then the `looks_like` chain from the game data, then a
`builtin` override of its model type, then the built-in procedural model. A set can start with a
single model and grow over time.

## Meshes from regular 3D tools

Any `file` or animation frame may be a Wavefront `.obj` mesh instead of a `.vox` file. The game
voxelizes it when it loads, using the material colors and the `map_Kd` texture of its `.mtl` file.
One OBJ unit is one map tile, so a Blender model built in meters has the right size; `mesh_height`
or `mesh_scale` change that. Models face `-Z` in the OBJ, which is north in game, with `+Y` up.

To bake a mesh into a `.vox` file ahead of time, for example to touch it up in MagicaVoxel:

```sh
tools/voxelize/build.sh
./voxelize model.obj model.vox --voxels-per-tile 8 --height 13
```

## Animations

Creatures can play voxel flipbook animations. Every frame is its own `.vox` or `.obj` file, so an
animation made in Blender can be exported as one OBJ per key frame and used directly.

```json
{
  "id": "mon_zombie",
  "animations": {
    "idle": { "files": [ "zombie/idle_0.vox", "zombie/idle_1.vox" ], "fps": 2 },
    "walk": { "files": [ "zombie/walk_0.obj", "zombie/walk_1.obj", "zombie/walk_2.obj", "zombie/walk_3.obj" ], "fps": 8 },
    "attack": { "files": [ "zombie/attack_0.vox", "zombie/attack_1.vox" ], "fps": 6 },
    "death": { "files": [ "zombie/death_0.vox", "zombie/death_1.vox", "zombie/death_2.vox" ], "fps": 6 }
  }
}
```

States: `idle`, `walk` while moving between tiles, `attack` while lunging, `death` once when the
monster dies (it does not loop). A missing state falls back to `idle`.

## Voxelized 2D tilesets

The 3D view style `Voxelized tileset` needs no voxel tileset at all: it turns every sprite of the
current 2D tileset into a voxel model on the fly. Floor sprites become pixel tiles, wall sprites
become extruded blocks, and standing sprites such as creatures, furniture and trees are inflated
from their silhouette into a rounded volume. Any 2D tileset, including your own, works this way.

## Model conventions

* The model stands on the floor: voxel layer 0 is the ground.
* The footprint is centered on the tile. A model wider than one tile, such as a tree, spills over its
  neighbours evenly.
* MagicaVoxel `z` is up. The front of creatures and vehicles faces MagicaVoxel `+y`, which is north
  in game.
* Palette colors are used as they are. A palette entry with the glass material, or with alpha below
  255, is drawn as translucent glass.

## Built-in model types

Built-in types that can be used with `builtin`:

`floor`, `wall`, `window`, `door`, `door_open`, `tree`, `pine`, `dead_tree`, `young_tree`, `shrub`,
`flower`, `tall_grass`, `water`, `stairs_down`, `stairs_up`, `hole`, `fence`, `rubble`, `boulder`,
`bed`, `table`, `chair`, `sofa`, `counter`, `cabinet`, `toilet`, `sink`, `bathtub`, `stove`, `screen`,
`crate`, `barrel`, `dumpster`, `sign`, `lamp`, `furniture`, `humanoid`, `zombie`, `quadruped`, `bird`,
`bug`, `robot`, `blob`, `fish`, `plant_creature`, `item_pile`, `vehicle`, `fire`, `splat`, `cloud`,
`cursor`, `machine`, `pole`, `stack`, `mound`, `mat`, `panel`, `pillar`, `stump`, `ramp`, `grave`,
`anvil`, `hedge`, `trap`, `item_gun`, `item_blade`, `item_tool`, `item_clothing`, `item_food`,
`item_bottle`, `item_book`, `item_ammo`, `item_corpse`, `held`, `rain`, `snow`, `bolt`, `marker`.

Materials for `floor`, `wall` and `window`: `generic`, `grass`, `dirt`, `sand`, `pavement`,
`sidewalk`, `concrete`, `wood`, `tile`, `carpet`, `rock`, `metal`, `brick`, `glass`, `snow`, `log`.

## Starter kit

`gfx/voxel/starter` holds every built-in model exported to `.vox` with a ready `voxelset.json`.
Copy the folder, rename it, open the models in MagicaVoxel and change what you like.

To export a fresh kit:

```sh
tools/voxel_export/build.sh
./voxel-export gfx/voxel/my_set "My voxel set"
```
