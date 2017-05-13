#include "rts-game.h"
#include <algorithm>

namespace game
{
    bool move(float2 & position, const float2 & target, float max_step)
    {
        const auto delta = target - position;
        const auto delta_len = length(delta);
        if(delta_len > max_step) 
        {
            position += delta*(max_step/delta_len);
            return false;
        }
        position = target;
        return true;
    }

    unit * get_nearest_enemy_unit(std::vector<unit> & units, const unit & u)
    {
        float best_dist = std::numeric_limits<float>::infinity();
        unit * other {};
        for(auto & v : units)
        {
            if(u.owner == v.owner) continue;
            float dist = distance2(u.position, v.position);
            if(dist < best_dist)
            {
                best_dist = dist;
                other = &v;
            }
        }
        return other;
    }

    float2 get_random_position(std::mt19937 & rng, int owner)
    {
        std::uniform_real_distribution<float> dist_x{0, 16};
        std::uniform_real_distribution<float> dist_y{0, 64};
        return {dist_x(rng)+owner*48.0f, dist_y(rng)};
    }
}

struct particle_vertex
{
    float2 offset;
    float2 texcoord;
};

struct particle_instance
{
    float3 position;
    float size;
    float3 color;
};

/////////////////
// game::state //
/////////////////

game::state::state()
{
    for(size_t i=0; i<32; ++i) units.push_back({0, 5, get_random_position(rng, 0), {+1,0}});
    for(size_t i=0; i<32; ++i) units.push_back({1, 5, get_random_position(rng, 1), {-1,0}});
}

void game::state::advance(float timestep)
{
    std::normal_distribution<float> ndist;

    // Move towards the nearest enemy unit, and open fire once we have reached a distance of five units
    for(auto & u : units)
    {
        u.cooldown = std::max(u.cooldown - timestep, 0.0f);
        if(auto enemy = get_nearest_enemy_unit(units, u))
        {
            u.direction = slerp(u.direction, normalize(enemy->position - u.position), 0.1f);
            if(distance2(u.position, enemy->position) > 25)
            {
                move(u.position, enemy->position, timestep*4);
            }
            else if(u.cooldown == 0)
            {
                bullets.push_back({u.owner, u.position, enemy->position});
                u.cooldown += 0.5f;
            }
        }
    }

    // Simulate movement of bullets
    for(auto it=begin(bullets); it!=end(bullets); )
    {
        if(move(it->position, it->target, timestep*20))
        {
            // Bullets which reach their destination deal one damage to all units within a radius of one
            for(auto & u : units)
            {
                if(distance2(it->position, u.position) < 1)
                {
                    --u.hp;
                }
            }

            // Generate some particles at the point of impact
            for(int i=0; i<50; ++i) 
            {
                auto dir = float3{ndist(rng),ndist(rng),ndist(rng)};
                particles.push_back({it->get_position(), dir+normalize(dir)*5.0f, team_colors[it->owner]*5.0f, 0.5f});
            }
            flashes.push_back({it->get_position(), team_colors[it->owner]*10.0f, 0.2f});

            it = bullets.erase(it);
        }
        else ++it;
    }

    // Respawn units that have been destroyed on this frame
    for(auto & u : units)
    {
        if(u.hp < 0)
        {
            // Generate some particles where unit was destroyed
            for(int i=0; i<100; ++i) 
            {
                auto dir = float3{ndist(rng),ndist(rng),ndist(rng)};
                particles.push_back({{u.position, 0.25f}, dir+normalize(dir)*5.0f, {6,4,2}, 1.0f});
            }
            flashes.push_back({{u.position, 0.25f}, {10,10,10}, 0.3f});

            // Reset unit to new location
            u.position = get_random_position(rng, u.owner);
            u.direction = {u.owner ? -1.0f : +1.0f, 0};
            u.hp = 5;
            u.cooldown = 0;
        }
    }

    // Push overlapping units apart
    for(auto & u : units)
    {
        for(auto & v : units)
        {
            if(&u == &v) continue;
            auto delta = v.position - u.position;
            auto delta_len2 = length2(delta);
            if(delta_len2 > 4) continue;
            auto delta_len = std::sqrt(delta_len2);
            delta *= (2-delta_len)*0.5f;
            u.position -= delta;
            v.position += delta;
        }
    }

    // Simulate particles
    const float3 gravity {0,0,-2};
    for(auto & p : particles)
    {
        p.position += p.velocity * timestep + gravity * (timestep*timestep/2);
        p.velocity += gravity * timestep;
        if(p.position.z < 0 && p.velocity.z < 0) p.velocity.z *= -0.5f;
        p.life -= timestep;
    }
    particles.erase(std::remove_if(begin(particles), end(particles), [](const particle & p) { return p.life <= 0; }), end(particles));

    // Simulate flashes
    for(auto & f : flashes) f.life -= timestep;
    flashes.erase(std::remove_if(begin(flashes), end(flashes), [](const flash & f) { return f.life <= 0; }), end(flashes));
}

/////////////////////
// game::resources //
/////////////////////

game::resources::resources(renderer & r, std::shared_ptr<scene_contract> contract)
{
    // Load meshes
    terrain_mesh = std::make_shared<gfx_mesh>(r.ctx, generate_box_mesh({0,0,-1}, {64,64,0}));
    unit0_mesh = std::make_shared<gfx_mesh>(r.ctx, transform(scaling_matrix(float3{0.1f}), load_mesh_from_obj(game::coords, "assets/f44a.obj")));
    unit1_mesh = std::make_shared<gfx_mesh>(r.ctx, transform(scaling_matrix(float3{0.1f}), load_mesh_from_obj(game::coords, "assets/cf105.obj")));
    bullet_mesh = std::make_shared<gfx_mesh>(r.ctx, apply_vertex_color(generate_box_mesh({-0.05f,-0.1f,-0.05f},{+0.05f,+0.1f,0.05f}), {2,2,2}));
    const particle_vertex particle_vertices[] {{{-0.5f,-0.5f}, {0,0}}, {{-0.5f,+0.5f}, {0,1}}, {{+0.5f,+0.5f}, {1,1}}, {{+0.5f,-0.5f}, {1,0}}};
    const uint32_t particle_indices[] {0, 1, 2, 0, 2, 3};
    particle_mesh = std::make_shared<gfx_mesh>(std::make_unique<static_buffer>(r.ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(particle_vertices), particle_vertices),
                                               std::make_unique<static_buffer>(r.ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(particle_indices), particle_indices), 6);

    // Load textures
    terrain_tex = std::make_shared<texture_2d>(r.ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({127,85,60,255}));
    unit0_tex = std::make_shared<texture_2d>(r.ctx, VK_FORMAT_R8G8B8A8_UNORM, load_image("assets/f44a.jpg"));
    unit1_tex = std::make_shared<texture_2d>(r.ctx, VK_FORMAT_R8G8B8A8_SRGB, load_image("assets/cf105.jpg"));
    bullet_tex = std::make_shared<texture_2d>(r.ctx, VK_FORMAT_R8G8B8A8_UNORM, generate_single_color_image({255,255,255,255}));
    particle_tex = std::make_shared<texture_2d>(r.ctx, VK_FORMAT_R8G8B8A8_SRGB, load_image("assets/particle.png"));

    // Create sampler
    VkSamplerCreateInfo sampler_info {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = 11;
    sampler_info.minLod = 0;
    linear_sampler = std::make_shared<sampler>(r.ctx, sampler_info);

    // Create our scene contract
    auto layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT},    
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto glow_layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT}
    });
    
    // Set up our shader pipeline
    auto vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/static.vert");
    auto frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/shader.frag");

    auto glow_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/glow.frag");
    auto particle_vert_shader = r.create_shader(VK_SHADER_STAGE_VERTEX_BIT, "assets/particle.vert");
    auto particle_frag_shader = r.create_shader(VK_SHADER_STAGE_FRAGMENT_BIT, "assets/particle.frag");

    auto mesh_vertex_format = r.create_vertex_format({{0, sizeof(mesh::vertex), VK_VERTEX_INPUT_RATE_VERTEX}}, {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, position)}, 
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, color)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, normal)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::vertex, texcoord)},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, tangent)},
        {5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::vertex, bitangent)},
        {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(mesh::vertex, bone_indices)},
        {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(mesh::vertex, bone_weights)}
    });
    pipeline = r.create_pipeline(layout, mesh_vertex_format, {vert_shader, frag_shader}, true, true, false);
    glow_pipeline = r.create_pipeline(layout, mesh_vertex_format, {vert_shader, glow_shader}, true, true, false);

    auto particle_layout = r.create_pipeline_layout(contract, {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT}
    });
    auto particle_vertex_format = r.create_vertex_format({
        {0, sizeof(particle_vertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(particle_instance), VK_VERTEX_INPUT_RATE_INSTANCE}
    }, {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(particle_vertex, offset)}, 
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(particle_vertex, texcoord)},
        {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(particle_instance, position)},
        {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(particle_instance, size)},
        {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(particle_instance, color)},
    });
    particle_pipeline = r.create_pipeline(particle_layout, particle_vertex_format, {particle_vert_shader, particle_frag_shader}, false, true, true);
}

/////////////////////
// game::draw(...) //
/////////////////////

void game::draw(draw_list & list, per_scene_uniforms & ps, const resources & r, const state & s)
{
    {
        auto descriptors = list.descriptor_set(*r.pipeline);
        descriptors.write_uniform_buffer(0, 0, list.upload_uniforms(per_static_object{translation_matrix(float3{0,0,0})}));
        descriptors.write_combined_image_sampler(1, 0, r.linear_sampler->get_vk_handle(), *r.terrain_tex);
        list.draw(*r.pipeline, descriptors, *r.terrain_mesh);
    }

    for(auto & f : s.flashes)
    {
        if(ps.u_num_point_lights < 64) ps.u_point_lights[ps.u_num_point_lights++] = {f.position, f.color*f.life};
    }

    for(auto & u : s.units)
    {
        auto descriptors = list.descriptor_set(*r.pipeline);
        descriptors.write_uniform_buffer(0, 0, list.upload_uniforms(per_static_object{u.get_model_matrix(), game::team_colors[u.owner]*std::max(u.cooldown*4-1.5f,0.0f)}));
        descriptors.write_combined_image_sampler(1, 0, r.linear_sampler->get_vk_handle(), u.owner ? *r.unit1_tex : *r.unit0_tex);
        list.draw(*r.pipeline, descriptors, u.owner ? *r.unit1_mesh : *r.unit0_mesh);
    }

    for(auto & b : s.bullets)
    {
        auto descriptors = list.descriptor_set(*r.glow_pipeline);
        descriptors.write_uniform_buffer(0, 0, list.upload_uniforms(per_static_object{b.get_model_matrix()}));
        list.draw(*r.glow_pipeline, descriptors, *r.bullet_mesh);
        if(ps.u_num_point_lights < 64) ps.u_point_lights[ps.u_num_point_lights++] = {b.get_position(), game::team_colors[b.owner]};
    }

    auto particle_descriptors = list.descriptor_set(*r.particle_pipeline);
    particle_descriptors.write_combined_image_sampler(0, 0, r.linear_sampler->get_vk_handle(), *r.particle_tex);
    list.begin_instances();
    for(auto & p : s.particles) list.write_instance(particle_instance{p.position, p.life/3, p.color});           
    list.draw(*r.particle_pipeline, particle_descriptors, *r.particle_mesh, list.end_instances(), sizeof(particle_instance));
}