#include "linalg.h"
using namespace linalg::aliases;

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

template<class T> void require_approx_equal(const linalg::vec<T,3> & a, const linalg::vec<T,3> & b)
{
    REQUIRE(a.x == Approx(b.x));
    REQUIRE(a.y == Approx(b.y));
    REQUIRE(a.z == Approx(b.z));
}
/*
template<class Transform> void test_transform(const Transform & t, bool is_rigid, bool is_scale_preserving)
{
    // Start by transforming three points using the supplied transformation
    const float3 p0 {1,2,3}, p1 {4,-5,2}, p2 {-3,1,-4};
    const float3 pp0 = transform_point(t, p0);
    const float3 pp1 = transform_point(t, p1);
    const float3 pp2 = transform_point(t, p2);

    // Test transform_vector(...)
    const float3 e1 = p1 - p0, e2 = p2 - p0, n = normalize(cross(e1, e2));
    const float3 ee1 = pp1 - pp0, ee2 = pp2 - pp0, nn = normalize(cross(ee1, ee2));
    require_approx_equal(transform_vector(t, e1), ee1);
    require_approx_equal(transform_vector(t, e2), ee2);

    // Test transform_tangent(...)
    require_approx_equal(transform_tangent(t, normalize(e1)), normalize(ee1));
    require_approx_equal(transform_tangent(t, normalize(e2)), normalize(ee2));

    // Test transform_normal(...)
    require_approx_equal(transform_normal(t, n), nn);

    if(is_rigid)
    {
        // Test transform_quat(...)
        const quatf q = rotation_quat(float3{0,0,1}, 1.0f);
        const quatf qq = transform_quat(t, q);
        require_approx_equal(qrot(qq, ee1), transform_vector(t, qrot(q, e1)));
        require_approx_equal(qrot(qq, ee2), transform_vector(t, qrot(q, e2)));

        // Test transform_matrix(...)
        const float4x4 m {{1,0,0,0},{0,0,1,0},{0,1,0,0},{0,0,0,1}};
        const float4x4 mm = transform_matrix(t, m);
        require_approx_equal(transform_vector(mm, ee1), transform_vector(t, transform_vector(m, e1)));
        require_approx_equal(transform_vector(mm, ee2), transform_vector(t, transform_vector(m, e2)));
    }

    if(is_scale_preserving)
    {
        // Test transform_scaling(...)
        const float3 s {1,1,2};
        const float3 ss = transform_scaling(t, s);
        require_approx_equal(ss * ee1, transform_vector(t, s * e1));
        require_approx_equal(ss * ee2, transform_vector(t, s * e2));
    }
}

TEST_CASE("transform functions", "[transform]")
{
    // Test a transform which apply non-uniform scales in one axis
    test_transform(float3x3{{1,0,0},{0,2,0},{0,0,1}}, false, false);
    test_transform(float3x3{{2,0,0},{0,3,0},{0,0,4}}, false, false);
    test_transform(float4x4{{-5,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}, false, false);

    // Test rigid transforms
    test_transform(rotation_matrix(normalize(quatf{1,2,3,4})) * translation_matrix(float3{2,5,3}), true, false);
    test_transform(float_pose{normalize(quatf{8,0,0,6}), {1,2,3}}, true, false);
    test_transform(float_pose{normalize(quatf{3,1,4,2}), {4,1,2}}, true, false);

    // Test transform which rearranges axes, some of which involve a handedness transform
    test_transform(float3x3{{1,0,0},{0,0,-1},{0,1,0}}, true, true); // rotation
    test_transform(float3x3{{1,0,0},{0,0,1},{0,1,0}}, true, true); // mirror
    test_transform(float4x4{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{0,0,0,1}}, true, true); // rotation
    test_transform(float4x4{{0,1,0,0},{0,0,1,0},{1,0,0,0},{0,0,0,1}}, true, true); // rotation
    test_transform(float4x4{{-1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}, true, true); // mirror
}
*/