#include <fbx.h>
#include <linalg.h>
using namespace linalg::aliases;

#include <iostream>
#include <fstream>
#include <chrono>

#include <GLFW/glfw3.h>
#pragma comment(lib, "opengl32.lib")

const fbx::node & find(const std::vector<fbx::node> & nodes, std::string_view name)
{
    for(auto & n : nodes) if(n.name == name) return n;
    throw std::runtime_error("missing node " + std::string(name));
}

struct vertex
{
    float3 position;
    float3 normal;
};

struct fbx_geometry
{
    std::vector<vertex> vertices; // Corresponds to polygon vertices
    std::vector<uint3> triangles;

    fbx_geometry(const fbx::node & node)
    {
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
        auto & normal_layer_node = find(node.children, "LayerElementNormal");
        auto & normal_layer_mapping_node = find(normal_layer_node.children, "MappingInformationType");
        auto mapping_information_type = std::get<std::string>(normal_layer_mapping_node.properties[0]);
        auto & normal_layer_reference_node = find(normal_layer_node.children, "ReferenceInformationType");
        auto reference_information_type = std::get<std::string>(normal_layer_reference_node.properties[0]);
        if(mapping_information_type != "ByPolygonVertex") throw std::runtime_error("unsupported MappingInformationType");
        if(reference_information_type != "Direct") throw std::runtime_error("unsupported ReferenceInformationType");
        auto & normals_array = find(normal_layer_node.children, "Normals");
        auto & normals = std::get<std::vector<double>>(normals_array.properties[0]);
        for(size_t i=0; i<vertices.size(); ++i)
        {
            vertices[i].normal = float3{double3{normals[i*3+0], normals[i*3+1], normals[i*3+2]}};
        }
    }
};

int main() try
{
    std::ifstream in("test.fbx", std::ifstream::binary);
    const auto doc = fbx::load(in);
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) std::cout << node << std::endl;

    std::vector<fbx_geometry> geoms;
    for(auto & n : find(doc.nodes, "Objects").children)
    {
        if(n.name == "Geometry")
        {
            geoms.push_back(fbx_geometry{n});
        }
    }

    glfwInit();
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);

    float3 camera_position {0,0,100};
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

        // Draw meshes
        glLoadMatrixf(&view_proj_matrix.x.x);
        for(auto & g : geoms)
        {
            glEnable(GL_DEPTH_TEST);
            glBegin(GL_TRIANGLES);
            for(auto & triangle : g.triangles)
            {
                for(auto index : triangle) 
                {
                    glColor3fv(&g.vertices[index].normal.x);
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