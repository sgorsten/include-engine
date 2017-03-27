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

struct fbx_geometry
{
    std::vector<float3> vertices;
    std::vector<int3> triangles;

    fbx_geometry(const fbx::node & node)
    {
        // Obtain vertices
        auto & vertices_node = find(node.children, "Vertices");
        if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
        auto & vertices_array = std::get<std::vector<double>>(vertices_node.properties[0]);
        for(size_t i=0; i<vertices_array.size(); i+=3)
        {
            vertices.push_back(float3{double3{vertices_array[i], vertices_array[i+1], vertices_array[i+2]}});
        }

        // Obtain polygon faces
        auto & indices_node = find(node.children, "PolygonVertexIndex");
        if(indices_node.properties.size() != 1) throw std::runtime_error("malformed PolygonVertexIndex");
        std::vector<int> face_indices;
        for(auto i : std::get<std::vector<int32_t>>(indices_node.properties[0]))
        {
            if(i < 0)
            {
                face_indices.push_back(~i);
                for(size_t j=2; j<face_indices.size(); ++j)
                {
                    triangles.push_back({face_indices[0], face_indices[j-1], face_indices[j]});
                }
                face_indices.clear();
            }
            else face_indices.push_back(i);
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
            glBegin(GL_TRIANGLES);
            for(auto & triangle : g.triangles)
            {
                for(auto index : triangle) glVertex3fv(&g.vertices[index].x);
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