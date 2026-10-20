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
    cursor,
    machine,
    pole,
    stack,
    mound,
    mat,
    panel,
    pillar,
    stump,
    ramp,
    grave,
    anvil,
    hedge,
    trap,
    vehicle,
    item_gun,
    item_blade,
    item_tool,
    item_clothing,
    item_food,
    item_bottle,
    item_book,
    item_ammo,
    item_corpse,
    held,
    rain,
    snow,
    bolt,
    marker,
    particle,
    spark,
    omt_ground,
    omt_block
};

enum vm_vehicle_bits : uint8_t {
    vmv_wheel = 1,
    vmv_board = 2,
    vmv_window = 4,
    vmv_seat = 8,
    vmv_roof = 16,
    vmv_engine = 32,
    vmv_light = 64,
    vmv_turret = 128
};

enum vm_vehicle_extra_bits : uint8_t {
    vmx_cargo = 1,
    vmx_door_open = 2,
    vmx_broken = 4,
    vmx_tank = 8,
    vmx_solar = 16,
    vmx_door = 32
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
const char *vm_kind_name( vm_kind kind );
const char *vm_material_name( vm_material material );
constexpr int vm_kind_count = static_cast<int>( vm_kind::omt_block ) + 1;

enum vm_part : uint8_t {
    vmp_whole = 0,
    vmp_body = 1,
    vmp_leg_a = 2,
    vmp_leg_b = 3,
    vmp_arm_a = 4,
    vmp_arm_b = 5
};

bool vm_has_limbs( vm_kind kind );
float vm_limb_pivot_y( vm_kind kind, uint8_t param, uint8_t part );
float vm_limb_pivot_z( vm_kind kind, uint8_t part );
constexpr int vm_material_count = static_cast<int>( vm_material::log ) + 1;
voxel_grid make_voxel_model( const vm_key &key );

#endif // CATA_SRC_VOXEL_MODELS_H
