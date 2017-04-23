#include "data-types.h"
#include <string>

image::image(int width, int height) : width{width}, height{height}
{
    auto p = std::malloc(width*height*4);
    if(!p) throw std::bad_alloc();
    pixels = std::unique_ptr<void, std_free_deleter>{p};
}

image::image(int width, int height, std::unique_ptr<void, std_free_deleter> pixels) : width{width}, height{height}, pixels{move(pixels)} 
{

}

mesh::bone_keyframe transform(const float4x4 & t, const mesh::bone_keyframe & keyframe)
{ 
    return {transform_vector(t, keyframe.translation), transform_quat(t, keyframe.rotation), transform_scaling_factors(t, keyframe.scaling)};
}

mesh transform(const float4x4 & t, mesh mesh)
{
    for(auto & v : mesh.vertices)
    {
        v.position  = transform_point(t, v.position);
        v.normal    = transform_normal(t, v.normal);
        v.tangent   = transform_direction(t, v.tangent);
        v.bitangent = transform_direction(t, v.bitangent);
    }
    for(auto & b : mesh.bones)
    {
        b.initial_pose = transform(t, b.initial_pose);
        b.model_to_bone_matrix = transform_matrix(t, b.model_to_bone_matrix);
    }
    for(auto & a : mesh.animations)
    {
        for(auto & k : a.keyframes) for(auto & lt : k.local_transforms) lt = transform(t, lt);
    }
    return mesh;
}