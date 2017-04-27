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

    void init_game(std::mt19937 & rng, std::vector<unit> & units)
    {
        for(size_t i=0; i<32; ++i) units.push_back({0, 5, get_random_position(rng, 0), {+1,0}});
        for(size_t i=0; i<32; ++i) units.push_back({1, 5, get_random_position(rng, 1), {-1,0}});
    }

    void advance_game(std::mt19937 & rng, std::vector<unit> & units, std::vector<bullet> & bullets, std::vector<particle> & particles, float timestep)
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
                for(int i=0; i<20; ++i) 
                {
                    auto dir = float3{ndist(rng),ndist(rng),ndist(rng)};
                    particles.push_back({it->get_position(), dir+normalize(dir)*3.0f, {3,2,1}, 0.5f});
                }

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
    }
}