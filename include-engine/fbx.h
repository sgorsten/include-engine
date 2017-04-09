#ifndef FBX_H
#define FBX_H

#include "linalg.h"

#include <string>
#include <vector>
#include <variant>
#include <optional>

namespace fbx
{
    // Abstract syntax tree of an FBX file, maps to both binary and textual representations
    namespace ast
    {
        struct boolean 
        { 
            uint8_t byte; 
            explicit operator bool() const { return static_cast<bool>(byte & 1); } 
        };

        using property_variant = std::variant
        <
            boolean,               // type 'C'
            int16_t,               // type 'Y'
            int32_t,               // type 'I'
            int64_t,               // type 'L'
            float,                 // type 'F'
            double,                // type 'D'
            std::vector<boolean>,  // type 'b'
            std::vector<int16_t>,  // type 'y'
            std::vector<int32_t>,  // type 'i'
            std::vector<int64_t>,  // type 'l'
            std::vector<float>,    // type 'f'
            std::vector<double>,   // type 'd'
            std::string,           // type 'S'
            std::vector<uint8_t>   // type 'R'
        >;

        class property
        {
            property_variant contents;

            struct size_visitor
            {
                template<class T> size_t operator() (const std::vector<T> & v) { return v.size(); }
                size_t operator() (...) { return 1; }
            };

            template<class U> struct element_visitor
            {
                size_t index;
                template<class T> U operator() (const std::vector<T> & v) { return operator()(v[index]); }
                template<class T> U operator() (const T & n) { return static_cast<U>(n); }
                U operator() (const std::string & s) { return {}; }
                U operator() (const boolean & b) { return b ? U{1} : U{0}; }
            };
        public:
            property(property_variant && contents) : contents{move(contents)} {}
        
            size_t size() const { return std::visit(size_visitor{}, contents); }
            template<class U> U get(size_t i=0) const { return std::visit(element_visitor<U>{i}, contents); }
            const std::string & get_string() const { return std::get<std::string>(contents); }
            void print(std::ostream & out) const;
        };

        struct node
        {
            std::string name;
            std::vector<property> properties;
            std::vector<node> children;
        };

        struct document
        {
            uint32_t version;
            std::vector<node> nodes;
        };

        document load(std::istream & in);
    }

    /////////////////////
    // FBX Scene Graph //
    /////////////////////

    using namespace linalg::aliases;

    enum class rotation_order { xyz, xzy, yzx, yxz, zxy, zyx, spheric_xyz };

    struct animation_keyframe
    {
        int64_t key;
        float value;
    };
    inline float evaluate_curve(const std::vector<animation_keyframe> & keyframes, int64_t key, float def_value)
    {
        if(keyframes.empty()) return def_value;
        if(key <= keyframes.front().key) return keyframes.front().value;
        for(size_t i=1; i<keyframes.size(); ++i) if(key <= keyframes[i].key)
        {
            const float t = (float)(key - keyframes[i-1].key)/(keyframes[i].key - keyframes[i-1].key);
            return keyframes[i-1].value*(1-t) + keyframes[i].value*t;
        }
        return keyframes.back().value;
    }

    struct vector_animation
    {
        std::vector<animation_keyframe> x,y,z;
        float3 evaluate(int64_t key, const float3 & def_value) { return {evaluate_curve(x,key,def_value.x), evaluate_curve(y,key,def_value.y), evaluate_curve(z,key,def_value.z)}; }
    };
    struct bone_animation
    {
        vector_animation lcl_position, lcl_rotation, lcl_scale;
    };
    struct animation
    {
        std::vector<bone_animation> bones;    
    };

    struct bone
    {
        std::string name;
        std::optional<size_t> parent_index;
        float4x4 initial_pose;
        float4x4 transform, transform_link;
    };

    struct geometry
    {
        struct vertex
        {
            float3 position;
            float3 normal;
            float2 texcoord;
        };
        struct bone_weights 
        { 
            uint4 indices; 
            float4 weights; 
        };

        int64_t id;
        std::vector<vertex> vertices; // Corresponds to polygon vertices
        std::vector<bone_weights> weights;
        std::vector<uint3> triangles;
        std::vector<bone> bones;
    };

    struct model
    {
        int64_t id;
        rotation_order rotation_order {rotation_order::xyz};
        float3 translation, rotation_offset, rotation_pivot; // Translation vectors
        float3 pre_rotation, rotation, post_rotation; // Euler angles in radians
        float3 scaling_offset, scaling_pivot; // Translation vectors
        float3 scaling; // Scaling factors

        std::vector<geometry> geoms;

        model(const ast::node & node);
        float4x4 get_model_matrix() const;
    };

    struct document
    {
        std::vector<model> models;
    };

    document load(const ast::document & doc);
}

std::ostream & operator << (std::ostream & out, const fbx::ast::boolean & b);
std::ostream & operator << (std::ostream & out, const fbx::ast::property & p);
std::ostream & operator << (std::ostream & out, const fbx::ast::node & n);

#endif
