#include <fbx.h>
#include <linalg.h>
using namespace linalg::aliases;

#include <iostream>
#include <fstream>
#include <chrono>

#include <GLFW/glfw3.h>
#pragma comment(lib, "opengl32.lib")

template<class T> constexpr T rad_to_deg = static_cast<T>(57.295779513082320876798154814105);
template<class T> constexpr T deg_to_rad = static_cast<T>(0.0174532925199432957692369076848);

const fbx::node & find(const std::vector<fbx::node> & nodes, std::string_view name)
{
    for(auto & n : nodes) if(n.name == name) return n;
    throw std::runtime_error("missing node " + std::string(name));
}

enum class fbx_rotation_order { xyz, xzy, yzx, yxz, zxy, zyx, spheric_xyz };

float4 quat_from_euler(fbx_rotation_order order, const float3 & angles)
{
    const float4 x = rotation_quat(float3{1,0,0}, angles.x), y = rotation_quat(float3{0,1,0}, angles.y), z = rotation_quat(float3{0,0,1}, angles.z);
    switch(order)
    {
    case fbx_rotation_order::xyz: return qmul(z, y, x);
    case fbx_rotation_order::xzy: return qmul(y, z, x);
    case fbx_rotation_order::yzx: return qmul(x, z, y);
    case fbx_rotation_order::yxz: return qmul(z, x, y);
    case fbx_rotation_order::zxy: return qmul(y, x, z);
    case fbx_rotation_order::zyx: return qmul(x, y, z);
    case fbx_rotation_order::spheric_xyz: throw std::runtime_error("spheric_xyz rotation order not yet supported");
    default: throw std::runtime_error("bad rotation_order");
    }
}

float3 read_vector3d_property(const fbx::node & prop)
{
    return float3{double3{std::get<double>(prop.properties[4]), std::get<double>(prop.properties[5]), std::get<double>(prop.properties[6])}};
}

struct fbx_model
{
    int64_t id;
    fbx_rotation_order rotation_order {fbx_rotation_order::xyz};
    float3 translation, rotation_offset, rotation_pivot; // Translation vectors
    float3 pre_rotation, rotation, post_rotation; // Euler angles in radians
    float3 scaling_offset, scaling_pivot; // Translation vectors
    float3 scaling; // Scaling factors

    fbx_model(const fbx::node & node)
    {
        id = std::get<int64_t>(node.properties[0]);

        auto & prop70 = find(node.children, "Properties70"); // TODO: Is this version dependant?
        for(auto & p : prop70.children)
        {
            if(p.name != "P") continue;
            auto & prop_name = std::get<std::string>(p.properties[0]);
            if(prop_name == "RotationOffset") rotation_offset = read_vector3d_property(p);
            if(prop_name == "RotationPivot") rotation_pivot = read_vector3d_property(p);
            if(prop_name == "ScalingOffset") scaling_offset = read_vector3d_property(p);
            if(prop_name == "ScalingPivot") scaling_pivot = read_vector3d_property(p);
            if(prop_name == "RotationOrder") rotation_order = static_cast<fbx_rotation_order>(std::get<int32_t>(p.properties[4])); // Note: Just a guess, need to see one of these in the wild
            if(prop_name == "PreRotation") pre_rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "PostRotation") post_rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "Lcl Translation") translation = read_vector3d_property(p);
            if(prop_name == "Lcl Rotation") rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "Lcl Scaling") scaling = read_vector3d_property(p);
        }
    }

    float4x4 get_model_matrix() const
    {
        // Derived from http://help.autodesk.com/view/FBX/2017/ENU/?guid=__files_GUID_10CDD63C_79C1_4F2D_BB28_AD2BE65A02ED_htm
        // LocalToParentTransform = T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp -1 
        return mul
        (
            translation_matrix(translation + rotation_offset + rotation_pivot),
            rotation_matrix(qmul
            (
                quat_from_euler(rotation_order, pre_rotation), 
                quat_from_euler(rotation_order, rotation), 
                qconj(quat_from_euler(rotation_order, post_rotation))
            )), 
            translation_matrix(-rotation_pivot + scaling_offset + scaling_pivot),
            scaling_matrix(scaling), 
            translation_matrix(-scaling_pivot)
        );        
    }
};

struct fbx_geometry
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

    template<class T, int M> void decode_attribute(linalg::vec<T,M> & attribute, const std::vector<double> & array, size_t index) 
    {
        for(int j=0; j<M; ++j) attribute[j] = static_cast<T>(array[index*M+j]);
    }

    template<class T, int M> void decode_layer(const fbx::node & node, std::string_view array_name, linalg::vec<T,M> vertex::*attribute) 
    {
        auto & array = std::get<std::vector<double>>(find(node.children, array_name).properties[0]);
        auto mapping_information_type = std::get<std::string>(find(node.children, "MappingInformationType").properties[0]);
        auto reference_information_type = std::get<std::string>(find(node.children, "ReferenceInformationType").properties[0]);
        if(mapping_information_type == "ByPolygonVertex") 
        {
            if(reference_information_type == "Direct")
            {
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, i);
            }
            else if(reference_information_type == "IndexToDirect")
            {
                auto & index_array = std::get<std::vector<int>>(find(node.children, std::string(array_name) + "Index").properties[0]);
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, static_cast<size_t>(index_array[i]));
            }
            else throw std::runtime_error("unsupported ReferenceInformationType: " + reference_information_type);    
        }
        else throw std::runtime_error("unsupported MappingInformationType: " + mapping_information_type);
        
    }

    fbx_geometry(const fbx::node & node)
    {
        id = std::get<int64_t>(node.properties[0]);

        // Obtain vertices
        auto & vertices_node = find(node.children, "Vertices");
        if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
        auto & vertices_array = std::get<std::vector<double>>(vertices_node.properties[0]);
        std::vector<float3> vertex_positions;
        for(size_t i=0; i<vertices_array.size(); i+=3) vertex_positions.push_back(float3{double3{vertices_array[i], vertices_array[i+1], vertices_array[i+2]}});

        // Obtain polygons
        auto & indices_node = find(node.children, "PolygonVertexIndex");
        if(indices_node.properties.size() != 1) throw std::runtime_error("malformed PolygonVertexIndex");

        size_t polygon_start = 0;
        for(auto i : std::get<std::vector<int32_t>>(indices_node.properties[0]))
        {
            // Detect end-of-polygon, indicated by a negative index
            const bool end_of_polygon = i < 0;
            if(end_of_polygon) i = ~i;

            // Store a polygon vertex
            vertices.push_back({vertex_positions[i]});

            // Generate triangles if necessary
            if(end_of_polygon)
            {
                for(size_t j=polygon_start+2; j<vertices.size(); ++j)
                {
                    triangles.push_back(uint3{linalg::vec<size_t,3>{polygon_start, j-1, j}});
                }
                polygon_start = vertices.size();
            }
        }

        // Obtain normals
        decode_layer(find(node.children, "LayerElementNormal"), "Normals", &vertex::normal);
        decode_layer(find(node.children, "LayerElementUV"), "UV", &vertex::texcoord);
    }
};

int main() try
{
    std::ifstream in("test.fbx", std::ifstream::binary);
    const auto doc = fbx::load(in);
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) std::cout << node << std::endl;

    std::vector<fbx_geometry> geoms;
    std::vector<fbx_model> models;
    for(auto & n : find(doc.nodes, "Objects").children)
    {
        if(n.name == "Geometry")
        {
            geoms.push_back(fbx_geometry{n});
        }
        if(n.name == "Model")
        {
            models.push_back(fbx_model{n});
        }
    }

    glfwInit();
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);

    float3 camera_position {0,0,50};
    float camera_yaw {0}, camera_pitch {0};
    double2 last_cursor;
    auto t0 = std::chrono::high_resolution_clock::now();
    while(!glfwWindowShouldClose(win))
    {
        glfwPollEvents();

        // Compute timestep
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto timestep = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;

        // Handle mouselook
        double2 cursor;
        glfwGetCursorPos(win, &cursor.x, &cursor.y);
        if(glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT))
        {
            const auto move = float2(cursor - last_cursor);
            camera_yaw -= move.x * 0.01f;
            camera_pitch = std::max(-1.5f, std::min(1.5f, camera_pitch - move.y * 0.01f));
        }
        last_cursor = cursor;

        // Handle WASD
        const float4 camera_orientation = qmul(rotation_quat(float3{0,1,0}, camera_yaw), rotation_quat(float3{1,0,0}, camera_pitch));
        if(glfwGetKey(win, GLFW_KEY_W)) camera_position -= qzdir(camera_orientation) * (timestep * 50);
        if(glfwGetKey(win, GLFW_KEY_A)) camera_position -= qxdir(camera_orientation) * (timestep * 50);
        if(glfwGetKey(win, GLFW_KEY_S)) camera_position += qzdir(camera_orientation) * (timestep * 50);
        if(glfwGetKey(win, GLFW_KEY_D)) camera_position += qxdir(camera_orientation) * (timestep * 50);

        // Determine matrices
        int width, height;
        glfwGetFramebufferSize(win, &width, &height);
        const auto proj_matrix = linalg::perspective_matrix(1.0f, (float)width/height, 10.0f, 1000.0f);
        const auto view_matrix = inverse(pose_matrix(camera_orientation, camera_position));
        const auto view_proj_matrix = mul(proj_matrix, view_matrix);

        // Clear the viewport
        glfwMakeContextCurrent(win);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for(auto & m : models)
        {
            const auto model_matrix = m.get_model_matrix();
            const auto model_view_proj_matrix = mul(view_proj_matrix, model_matrix);
            glLoadMatrixf(&model_view_proj_matrix.x.x);
        }

        // Draw meshes
        //glLoadMatrixf(&view_proj_matrix.x.x);
        for(auto & g : geoms)
        {
            glEnable(GL_DEPTH_TEST);
            glBegin(GL_TRIANGLES);
            for(auto & triangle : g.triangles)
            {
                for(auto index : triangle) 
                {
                    glColor3f(g.vertices[index].texcoord.x, g.vertices[index].texcoord.y, 0);
                    glVertex3fv(&g.vertices[index].position.x);
                }
            }
            glEnd();
        }

        // Present
        glfwSwapBuffers(win);
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << "\n" << e.what() << std::endl;
    return EXIT_FAILURE;
}