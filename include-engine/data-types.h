#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include "linalg.h"
using namespace linalg::aliases;

#include <memory>
#include <vector>
#include <optional>

// Abstract over determining the number of elements in a collection
template<class T, uint32_t N> constexpr uint32_t countof(const T (&)[N]) { return N; }
template<class T, uint32_t N> constexpr uint32_t countof(const std::array<T,N> &) { return N; }
template<class T> constexpr uint32_t countof(const std::initializer_list<T> & ilist) { return static_cast<uint32_t>(ilist.size()); }
template<class T> constexpr uint32_t countof(const std::vector<T> & vec) { return static_cast<uint32_t>(vec.size()); }

// A lightweight non-owning reference type for passing contiguous chunks of memory to a function
template<class T> struct array_view
{
    const T * data;
    uint32_t size;

    template<uint32_t N> array_view(const T (& array)[N]) : data{array}, size{countof(array)} {}
    template<uint32_t N> array_view(const std::array<T,N> & array) : data{array.data()}, size{countof(array)} {}
    array_view(std::initializer_list<T> ilist) : data{ilist.begin()}, size{countof(ilist)} {}
    array_view(const std::vector<T> & vec) : data{vec.data()}, size{countof(vec)} {}
};
template<class T> const T * begin(const array_view<T> & view) { return view.data; }
template<class T> const T * end(const array_view<T> & view) { return view.data + view.size; }

// A container for storing contiguous 2D bitmaps of pixels
struct std_free_deleter { void operator() (void * p) { std::free(p); } };
class image
{
    int width {}, height {};
    std::unique_ptr<void, std_free_deleter> pixels;
public:
    image() {}
    image(int width, int height);
    image(int width, int height, std::unique_ptr<void, std_free_deleter> pixels);

    int get_width() const { return width; }
    int get_height() const { return height; }
    int get_channels() const { return 4; }
    const void * get_pixels() const { return pixels.get(); }
};

// Value type which holds mesh information
struct mesh
{
    struct bone_keyframe
    {
        float3 translation;
        float4 rotation;
        float3 scaling;
        float4x4 get_local_transform() const { return mul(translation_matrix(translation), rotation_matrix(rotation), scaling_matrix(scaling)); }
    };
    struct bone
    {
        std::string name;
        std::optional<size_t> parent_index;
        bone_keyframe initial_pose;
        float4x4 model_to_bone_matrix;
    };
    struct vertex
    {
        float3 position, color, normal;
        float2 texcoord;
        float3 tangent, bitangent; // Gradient of texcoord.x and texcoord.y relative to position
        uint4 bone_indices;
        float4 bone_weights;
    };
    struct keyframe
    {
        int64_t key;
        std::vector<bone_keyframe> local_transforms;
    };
    struct animation
    {
        std::string name;
        std::vector<keyframe> keyframes;
    };
    struct material
    {
        std::string name;
        size_t first_triangle, num_triangles;
    };
    std::vector<vertex> vertices;
    std::vector<uint3> triangles;
    std::vector<bone> bones;
    std::vector<animation> animations;
    std::vector<material> materials;

    float4x4 get_bone_pose(const std::vector<bone_keyframe> & bone_keyframes, size_t index) const
    {
        auto & b = bones[index];
        return b.parent_index ? mul(get_bone_pose(bone_keyframes, *b.parent_index), bone_keyframes[index].get_local_transform()) : bone_keyframes[index].get_local_transform();
    }

    float4x4 get_bone_pose(size_t index) const
    {
        auto & b = bones[index];
        return b.parent_index ? mul(get_bone_pose(*b.parent_index), b.initial_pose.get_local_transform()) : b.initial_pose.get_local_transform();
    }
};

#endif
