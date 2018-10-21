#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <memory>           // For std::unique_ptr<T>, std::shared_ptr<T>
#include <string>           // For std::string
#include <vector>           // For std::vector<T>
#include <variant>          // For std::variant<T...>
#include <optional>         // For std::optional<T>
#include <functional>       // For std::function<T>

#include <vulkan/vulkan.h>  // For VkImageViewType, etc...
#include <GLFW/glfw3.h>     // For input enums, etc.

#include "utility.h"        // For narrow(...)
#include "linalg.h"         // For float3, etc...
using namespace linalg::aliases;

// This provides a subset of the functionality of std::byte
enum class byte : uint8_t {};

// Abstract over determining the number of elements in a collection
template<class T, size_t N> constexpr size_t countof(const T (&)[N]) { return N; }
template<class T, size_t N> constexpr size_t countof(const std::array<T,N> &) { return N; }
template<class T> constexpr size_t countof(const std::initializer_list<T> & ilist) { return ilist.size(); }
template<class T> constexpr size_t countof(const std::vector<T> & vec) { return vec.size(); }

// A lightweight non-owning reference type for passing contiguous chunks of memory to a function
template<class T> struct array_view
{
    const T * data;
    size_t size;

    template<size_t N> array_view(const T (& array)[N]) : data{array}, size{countof(array)} {}
    template<size_t N> array_view(const std::array<T,N> & array) : data{array.data()}, size{countof(array)} {}
    array_view(std::initializer_list<T> ilist) : data{ilist.begin()}, size{countof(ilist)} {}
    array_view(const std::vector<T> & vec) : data{vec.data()}, size{countof(vec)} {}
    const T & operator [] (int i) const { return data[i]; }
    const T * begin() const { return data; }
    const T * end() const { return data + size; }
};
template<class T> const T * begin(const array_view<T> & view) { return view.begin(); }
template<class T> const T * end(const array_view<T> & view) { return view.end(); }

// A container for storing contiguous 2D bitmaps of pixels
size_t compute_image_size(int2 dims, VkFormat formats);
struct std_free_deleter { void operator() (void * p) { std::free(p); } };
class image
{
    int2 dims;
    VkFormat format {VK_FORMAT_UNDEFINED};
    std::unique_ptr<byte, std_free_deleter> pixels;
public:
    image() {}
    image(int2 dims, VkFormat format);
    image(int2 dims, VkFormat format, std::unique_ptr<byte, std_free_deleter> pixels);

    int get_width() const { return dims.x; }
    int get_height() const { return dims.y; }
    VkFormat get_format() const { return format; }
    const byte * get_pixels() const { return pixels.get(); }
    byte * get_pixels() { return pixels.get(); }
};

// A value type representing an abstract direction vector in 3D space, independent of any coordinate system
enum class coord_axis { forward, back, left, right, up, down, north=forward, east=right, south=back, west=left };
constexpr float dot(coord_axis a, coord_axis b)
{
    constexpr float table[6][6] {{+1,-1,0,0,0,0},{-1,+1,0,0,0,0},{0,0,+1,-1,0,0},{0,0,-1,+1,0,0},{0,0,0,0,+1,-1},{0,0,0,0,-1,+1}};
    return table[static_cast<int>(a)][static_cast<int>(b)];
}

// A concrete 3D coordinate system with defined x, y, and z axes
struct coord_system
{
    coord_axis x_axis, y_axis, z_axis;
    constexpr float3 get_axis(coord_axis a) const { return {dot(x_axis, a), dot(y_axis, a), dot(z_axis, a)}; }
    constexpr float3 get_left() const { return get_axis(coord_axis::left); }
    constexpr float3 get_right() const { return get_axis(coord_axis::right); }
    constexpr float3 get_up() const { return get_axis(coord_axis::up); }
    constexpr float3 get_down() const { return get_axis(coord_axis::down); }
    constexpr float3 get_forward() const { return get_axis(coord_axis::forward); }
    constexpr float3 get_back() const { return get_axis(coord_axis::back); }
};
inline float3x3 make_transform(const coord_system & from, const coord_system & to) { return {to.get_axis(from.x_axis), to.get_axis(from.y_axis), to.get_axis(from.z_axis)}; }
inline float4x4 make_transform_4x4(const coord_system & from, const coord_system & to) { return {{to.get_axis(from.x_axis),0}, {to.get_axis(from.y_axis),0}, {to.get_axis(from.z_axis),0}, {0,0,0,1}}; }

namespace linalg
{
    // Support for rigid poses in three dimensions, defined by a rotation quaternion and a translation vector
    template<class T> struct pose { quat<T> orientation {0,0,0,1}; vec<T,3> position {0,0,0}; };
    template<class T> pose<T> mul            (const pose<T> & a, const pose<T> & b)  { return {a.orientation*b.orientation, a.position + qrot(a.orientation, b.position)}; }
    template<class T, class... R> pose<T> mul(const pose<T> & a, R... r)             { return mul(a, mul(r...)); }
    template<class T> pose<T> inverse(const pose<T> & p)                             { auto q = conjugate(p.orientation); return {q, qrot(q,-p.position)}; }
    template<class T> pose<T> nlerp  (const pose<T> & a, const pose<T> & b, float t) { return {nlerp(a.orientation, b.orientation, t), lerp(a.position, b.position, t)}; }
    template<class T> pose<T> slerp  (const pose<T> & a, const pose<T> & b, float t) { return {slerp(a.orientation, b.orientation, t), lerp(a.position, b.position, t)}; }
    template<class T> mat<T,4,4> pose_matrix(const pose<T> & a) { return pose_matrix(a.orientation, a.position); }

    // A vector is the difference between two points in 3D space, possessing both direction and magnitude
    template<class T> vec<T,3> transform_vector  (const mat<T,4,4> & m, const vec<T,3> & vector)   { return (m*vec<T,4>{vector,0}).xyz(); }
    template<class T> vec<T,3> transform_vector  (const mat<T,3,3> & m, const vec<T,3> & vector)   { return m * vector; }
    template<class T> vec<T,3> transform_vector  (const pose<T>    & p, const vec<T,3> & vector)   { return qrot(p.orientation, vector); }

    // A point is a specific location within a 3D space
    template<class T> vec<T,3> transform_point   (const mat<T,4,4> & m, const vec<T,3> & point)    { auto r=m * vec<T,4>{point,1}; return r.xyz()/r.w; }
    template<class T> vec<T,3> transform_point   (const mat<T,3,3> & m, const vec<T,3> & point)    { return transform_vector(m, point); }
    template<class T> vec<T,3> transform_point   (const pose<T>    & p, const vec<T,3> & point)    { return p.position + transform_vector(p, point); }

    // A tangent is a unit-length vector which is parallel to a piece of geometry, such as a surface or a curve
    template<class T> vec<T,3> transform_tangent (const mat<T,4,4> & m, const vec<T,3> & tangent)  { return normalize(transform_vector(m, tangent)); }
    template<class T> vec<T,3> transform_tangent (const mat<T,3,3> & m, const vec<T,3> & tangent)  { return normalize(transform_vector(m, tangent)); }
    template<class T> vec<T,3> transform_tangent (const pose<T>    & p, const vec<T,3> & tangent)  { return transform_vector(p, tangent); }

    // A normal is a unit-length bivector which is perpendicular to a piece of geometry, such as a surface or a curve
    template<class T> vec<T,3> transform_normal  (const mat<T,4,4> & m, const vec<T,3> & normal)   { return normalize(transform_vector(inverse(transpose(m)), normal)) * (determinant(m) < 0 ? -1.0f : 1.0f); }
    template<class T> vec<T,3> transform_normal  (const mat<T,3,3> & m, const vec<T,3> & normal)   { return normalize(transform_vector(inverse(transpose(m)), normal)) * (determinant(m) < 0 ? -1.0f : 1.0f); }
    template<class T> vec<T,3> transform_normal  (const pose<T>    & p, const vec<T,3> & normal)   { return transform_vector(p, normal); }

    // A quaternion can describe both a rotation and a uniform scaling in 3D space
    template<class T> quat<T> transform_quat     (const mat<T,4,4> & m, const quat<T> & quat)      { return {transform_vector(m, quat.xyz()) * (determinant(m) < 0 ? -1.0f : 1.0f), quat.w}; }
    template<class T> quat<T> transform_quat     (const mat<T,3,3> & m, const quat<T> & quat)      { return {transform_vector(m, quat.xyz()) * (determinant(m) < 0 ? -1.0f : 1.0f), quat.w}; }
    template<class T> quat<T> transform_quat     (const pose<T>    & p, const quat<T> & quat)      { return {transform_vector(p, quat.xyz()), quat.w}; }

    // A matrix can describe a general transformation of homogeneous coordinates in projective space
    template<class T> mat<T,4,4> transform_matrix(const mat<T,4,4> & m, const mat<T,4,4> & matrix) { return m * matrix * inverse(m); }
    template<class T> mat<T,4,4> transform_matrix(const mat<T,3,3> & m, const mat<T,4,4> & matrix) { return transform_matrix({{m[0],0},{m[1],0},{m[2],0},{0,0,0,1}}, matrix); }
    template<class T> mat<T,4,4> transform_matrix(const pose<T>    & p, const mat<T,4,4> & matrix) { return transform_matrix(pose_matrix(p), matrix); }

    // Scaling factors are not a vector, they are a compact representation of a scaling matrix
    template<class T> vec<T,3> transform_scaling (const mat<T,4,4> & m, const vec<T,3> & scaling)  { return diagonal(transform_matrix(m, scaling_matrix(scaling))).xyz(); }
    template<class T> vec<T,3> transform_scaling (const mat<T,3,3> & m, const vec<T,3> & scaling)  { return diagonal(transform_matrix(m, scaling_matrix(scaling))).xyz(); }
    template<class T> vec<T,3> transform_scaling (const pose<T>    & p, const vec<T,3> & scaling)  { return transform_scaling(pose_matrix(p), scaling); }
}

using float_pose = linalg::pose<float>;

// Value type which holds mesh information
struct mesh
{
    struct bone_keyframe
    {
        float3 translation;
        quatf rotation;
        float3 scaling;
        float4x4 get_local_transform() const { return translation_matrix(translation) * rotation_matrix(rotation) * scaling_matrix(scaling); }
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
        return b.parent_index ? get_bone_pose(bone_keyframes, *b.parent_index) * bone_keyframes[index].get_local_transform() : bone_keyframes[index].get_local_transform();
    }

    float4x4 get_bone_pose(size_t index) const
    {
        auto & b = bones[index];
        return b.parent_index ? get_bone_pose(*b.parent_index) * b.initial_pose.get_local_transform() : b.initial_pose.get_local_transform();
    }
};

template<class Transform> mesh::bone_keyframe transform(const Transform & t, const mesh::bone_keyframe & kf) 
{ 
    mesh::bone_keyframe r;
    r.translation = transform_vector(t, kf.translation);
    r.rotation = transform_quat(t, kf.rotation);
    r.scaling = transform_scaling(t, kf.scaling);
    return r; //return {transform_vector(t, kf.translation), transform_quat(t, kf.rotation), transform_scaling(t, kf.scaling)}; 
}
template<class Transform> mesh::bone transform(const Transform & t, const mesh::bone & b) { return {b.name, b.parent_index, transform(t,b.initial_pose), transform_matrix(t,b.model_to_bone_matrix)}; }
template<class Transform> mesh::vertex transform(const Transform & t, const mesh::vertex & v) { return {transform_point(t,v.position), v.color, transform_normal(t,v.normal), v.texcoord, transform_tangent(t,v.tangent), transform_tangent(t,v.bitangent), v.bone_indices, v.bone_weights}; }
template<class Transform> mesh transform(const Transform & t, mesh m)
{
    for(auto & v : m.vertices) v = transform(t,v);
    for(auto & b : m.bones) b = transform(t,b);
    for(auto & a : m.animations) for(auto & k : a.keyframes) for(auto & lt : k.local_transforms) lt = transform(t, lt);
    return m;
}

// Reflection information for a single shader
struct shader_info
{
    struct type;
    enum scalar_type { uint_, int_, float_, double_ };
    struct matrix_layout { uint32_t stride; bool row_major; };
    struct structure_member { std::string name; std::unique_ptr<const type> type; std::optional<uint32_t> offset; };
    struct numeric { scalar_type scalar; uint32_t row_count, column_count; std::optional<matrix_layout> matrix_layout; };
    struct sampler { scalar_type channel; VkImageViewType view_type; bool multisampled, shadow; };
    struct array { std::unique_ptr<const type> element; uint32_t length; std::optional<uint32_t> stride; };
    struct structure { std::string name; std::vector<structure_member> members; };
    struct type { std::variant<sampler, numeric, array, structure> contents; };
    struct descriptor { uint32_t set, binding; std::string name; type type; };

    VkShaderStageFlagBits stage;
    std::string name;
    std::vector<descriptor> descriptors;
};

#endif
