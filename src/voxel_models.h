#pragma once
#ifndef CATA_SRC_VOXEL_MODELS_H
#define CATA_SRC_VOXEL_MODELS_H

#include <cstdint>

#include "voxel_model.h"

enum class vm_kind : uint8_t {
    floor,
    wall,
    window,
    door,
    door_open,
    tree,
    pine,
    dead_tree,
    young_tree,
    shrub,
    flower,
    tall_grass,
    water,
    stairs_down,
    stairs_up,
    hole,
    fence,
    rubble,
    boulder,
    bed,
    table,
    chair,
    sofa,
    counter,
    cabinet,
    toilet,
    sink,
    bathtub,
    stove,
    screen,
    crate,
    barrel,
    dumpster,
    sign,
    lamp,
    furniture,
    humanoid,
    zombie,
    quadruped,
    bird,
    bug,
    robot,
    blob,
    fish,
    plant_creature,
    item_pile,
    veh_frame,
    veh_wheel,
    veh_body,
    veh_window,
    veh_seat,
    veh_part,
    fire,
    splat,
    cloud,
    cursor
};

enum class vm_material : uint8_t {
    generic,
    grass,
    dirt,
    sand,
    pavement,
    sidewalk,
    concrete,
    wood,
    tile,
    carpet,
    rock,
    metal,
    brick,
    glass,
    snow,
    log
};

struct vm_key {
    vm_kind kind = vm_kind::floor;
    vm_material material = vm_material::generic;
    uint8_t variant = 0;
    uint8_t param = 0;
    voxel_rgb color;
    voxel_rgb color2;

    uint64_t pack() const;
};

bool vm_is_emissive( vm_kind kind );
voxel_grid make_voxel_model( const vm_key &key );

#endif // CATA_SRC_VOXEL_MODELS_H
