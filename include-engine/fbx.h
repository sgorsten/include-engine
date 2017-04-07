#ifndef FBX_H
#define FBX_H

#include "linalg.h"

#include <string>
#include <vector>
#include <variant>

namespace fbx
{
    ////////////////////////////
    // FBX document structure //
    ////////////////////////////

    struct boolean 
    { 
        uint8_t byte; 
        explicit operator bool() const { return static_cast<bool>(byte & 1); } 
    };

    using property = std::variant
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
    document load_ascii(FILE * f);

    /////////////////////
    // FBX Scene Graph //
    /////////////////////

    using namespace linalg::aliases;

    enum class rotation_order { xyz, xzy, yzx, yxz, zxy, zyx, spheric_xyz };

    struct geometry
    {
        struct vertex
        {
            float3 position;
            float3 normal;
            float2 texcoord;
        };

        int64_t id;
        std::vector<vertex> vertices; // Corresponds to polygon vertices
        std::vector<uint3> triangles;

        geometry(const node & node);
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

        model(const node & node);
        float4x4 get_model_matrix() const;
    };

    std::vector<model> load_models(const fbx::document & doc);
}

std::ostream & operator << (std::ostream & out, const fbx::boolean & b);
std::ostream & operator << (std::ostream & out, const fbx::property & p);
std::ostream & operator << (std::ostream & out, const fbx::node & n);

#endif
