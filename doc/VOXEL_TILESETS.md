# Voxel tilesets

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

Lookup order for an object: its exact `id`, then the `looks_like` chain from the game data, then a
`builtin` override of its model type, then the built-in procedural model. A set can start with a
single model and grow over time.

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
