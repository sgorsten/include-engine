#ifndef RTS_GAME_H
#define RTS_GAME_H

#include "renderer.h"
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

        float3 get_position() const { return {position,0.5f}; }
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

        float3 get_position() const { return {position,0.75f}; }
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

    // Immutable GPU resources used during rendering
    struct resources
    {
        std::shared_ptr<scene_material> standard_mtl;
        std::shared_ptr<scene_material> glow_mtl;
        std::shared_ptr<scene_material> particle_mtl;
        std::shared_ptr<gfx_mesh> terrain_mesh;
        std::shared_ptr<gfx_mesh> unit0_mesh;
        std::shared_ptr<gfx_mesh> unit1_mesh;
        std::shared_ptr<gfx_mesh> bullet_mesh;
        std::shared_ptr<gfx_mesh> particle_mesh;
        std::shared_ptr<texture> terrain_tex;
        std::shared_ptr<texture> unit0_tex;
        std::shared_ptr<texture> unit1_tex;
        std::shared_ptr<texture> bullet_tex;
        std::shared_ptr<texture> particle_tex;
        std::shared_ptr<sampler> linear_sampler;

        resources(renderer & r, std::shared_ptr<scene_contract> contract);
    };

    // Uniforms which are constant for the entire scene
    struct point_light
    {
	    alignas(16) float3 u_position;
	    alignas(16) float3 u_color;
    };

    struct per_scene_uniforms
    {
        alignas(16) float4x4 shadow_map_matrix;
        alignas(16) float3 shadow_light_pos;
	    alignas(16) float3 ambient_light;
	    alignas(16) float3 light_direction;
	    alignas(16) float3 light_color;
	    alignas(16) point_light u_point_lights[64];
	    alignas(16) int u_num_point_lights;
    };

    // Uniforms which are constant within a single viewport
    struct per_view_uniforms
    {
	    alignas(16) float4x4 view_proj_matrix;
	    alignas(16) float3 eye_position;
	    alignas(16) float3 eye_x_axis;
	    alignas(16) float3 eye_y_axis;
    };

    // Uniforms which are constant within a single draw call
    struct per_static_object
    {
        alignas(16) float4x4 model_matrix;
        alignas(16) float3 emissive_mtl;
    };

    void draw(draw_list & list, per_scene_uniforms & ps, const resources & r, const state & s);
}

#endif