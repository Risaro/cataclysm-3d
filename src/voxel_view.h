#pragma once
#ifndef CATA_SRC_VOXEL_VIEW_H
#define CATA_SRC_VOXEL_VIEW_H

#include <cstdint>
#include <optional>
#include <vector>

#include "coordinates.h"
#include "coords_fwd.h"
#include "point.h"

namespace voxel_view
{

struct overlays {
    bool explosion = false;
    tripoint_bub_ms explosion_center;
    int explosion_radius = 0;
    std::vector<tripoint_bub_ms> custom_explosion;
    bool bullet = false;
    tripoint_bub_ms bullet_pos;
    std::vector<tripoint_bub_ms> line;
    std::vector<tripoint_bub_ms> cursors;
    std::vector<tripoint_bub_ms> highlights;
};

bool active();
void set_overlays( const overlays &state );
bool animating();
int animation_interval();
bool render( const tripoint_bub_ms &center, int width, int height, std::vector<uint8_t> &pixels );
void orbit( float dx, float dy );
void rotate_step( int steps );
void tilt_step( int steps );
void reset_camera();
float camera_yaw_degrees();
void cycle_camera_mode();
void set_hover( const point &pixel, const point &size );
bool hover_changed( const point &pixel, const point &size );
std::optional<point> project( const tripoint_bub_ms &p, float lift );
std::optional<tripoint_bub_ms> pick( const point &pixel, const point &size );
point_rel_ms screen_to_map( const point_rel_ms &d );
point_rel_ms map_to_screen( const point_rel_ms &d );
void shutdown();

} // namespace voxel_view

#endif // CATA_SRC_VOXEL_VIEW_H
