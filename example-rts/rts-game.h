#ifndef RTS_GAME_H
#define RTS_GAME_H
#include "data-types.h"
#include <random>

namespace game
{
    constexpr coord_system coords {coord_axis::east, coord_axis::north, coord_axis::up};
    constexpr float3 team_colors[] {{0.5f,0.5f,0.0f}, {0.2f,0.2f,1.0f}};

    struct unit
    {
        int owner;
        int hp;
        float2 position;
        float2 direction;
        float cooldown;

        float3 get_position() const { return {position,0}; }
        float3 get_direction() const { return {direction,0}; }
        float4 get_orientation() const { return rotation_quat(coords.get_axis(coord_axis::north), get_direction()); }
        float_pose get_pose() const { return {get_orientation(), get_position()}; }
        float4x4 get_model_matrix() const { return pose_matrix(get_pose()); }
    };

    struct bullet
    {
        int owner;
        float2 position;
        float2 target;

        float3 get_position() const { return {position,0.25f}; }
        float3 get_direction() const { return {normalize(target-position),0}; }
        float4 get_orientation() const { return rotation_quat(coords.get_axis(coord_axis::north), get_direction()); }
        float_pose get_pose() const { return {get_orientation(), get_position()}; }
        float4x4 get_model_matrix() const { return pose_matrix(get_pose()); }
    };

    struct particle
    {
        float3 position, velocity, color; 
        float life;
    };

    struct flash
    {
        float3 position;
        float3 color;
        float life;
    };

    struct state
    {
        std::mt19937 rng;
        std::vector<unit> units;
        std::vector<bullet> bullets;
        std::vector<particle> particles;
        std::vector<flash> flashes;
        state();
        void advance(float timestep);
    };
}

#endif