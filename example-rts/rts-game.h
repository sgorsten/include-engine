#ifndef RTS_GAME_H
#define RTS_GAME_H
#include "data-types.h"
#include <random>

constexpr coord_system game_coords {coord_axis::east, coord_axis::north, coord_axis::up};

struct unit
{
    int owner;
    int hp;
    float2 position;
    float2 direction;
    float cooldown;

    float3 get_position() const { return {position,0}; }
    float3 get_direction() const { return {direction,0}; }
    float4 get_orientation() const { return rotation_quat(game_coords.get_axis(coord_axis::north), get_direction()); }
    float_pose get_pose() const { return {get_orientation(), get_position()}; }
    float4x4 get_model_matrix() const { return pose_matrix(get_pose()); }
};

struct bullet
{
    float2 position;
    float2 target;

    float3 get_position() const { return {position,0}; }
    float3 get_direction() const { return {normalize(target-position),0}; }
    float4 get_orientation() const { return rotation_quat(game_coords.get_axis(coord_axis::north), get_direction()); }
    float_pose get_pose() const { return {get_orientation(), get_position()}; }
    float4x4 get_model_matrix() const { return pose_matrix(get_pose()); }
};

void init_game(std::mt19937 & rng, std::vector<unit> & units);
void advance_game(std::mt19937 & rng, std::vector<unit> & units, std::vector<bullet> & bullets, float timestep);

#endif