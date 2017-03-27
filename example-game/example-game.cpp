#include "../include-engine/fbx.h"
#include <iostream>
#include <fstream>
#include <string_view>

#define GLFW_INCLUDE_GLU
#include <GLFW/glfw3.h>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

const fbx::node & find(const std::vector<fbx::node> & nodes, std::string_view name)
{
    for(auto & n : nodes) if(n.name == name) return n;
    throw std::runtime_error("missing node " + std::string(name));
}

struct fbx_geometry
{
    std::vector<double> vertices;
    std::vector<int> triangles;
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
            fbx_geometry geom;
            auto & vertices_node = find(n.children, "Vertices");
            if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
            auto & vertices_array = std::get<std::vector<double>>(vertices_node.properties[0]);
            geom.vertices.assign(begin(vertices_array), end(vertices_array));

            auto & indices_node = find(n.children, "PolygonVertexIndex");
            if(indices_node.properties.size() != 1) throw std::runtime_error("malformed PolygonVertexIndex");
            std::vector<int> face_indices;
            for(auto i : std::get<std::vector<int32_t>>(indices_node.properties[0]))
            {
                if(i < 0)
                {
                    face_indices.push_back(~i);
                    for(size_t j=2; j<face_indices.size(); ++j)
                    {
                        geom.triangles.push_back(face_indices[0]);
                        geom.triangles.push_back(face_indices[j-1]);
                        geom.triangles.push_back(face_indices[j]);
                    }
                    face_indices.clear();
                }
                else face_indices.push_back(i);
            }

            geoms.push_back(geom);
        }
    }

    glfwInit();
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);
    glfwMakeContextCurrent(win);

    while(!glfwWindowShouldClose(win))
    {
        glLoadIdentity();
        gluPerspective(45, (double)640/480, 10, 1000);
        gluLookAt(0, 0, 100, 0, 0, 0, 0, 1, 0);

        glClear(GL_COLOR_BUFFER_BIT);
        for(auto & g : geoms)
        {
            glBegin(GL_TRIANGLES);
            for(size_t i=0; i<g.triangles.size(); i+=3)
            {
                for(int j : {g.triangles[i], g.triangles[i+1], g.triangles[i+2]})
                {
                    glVertex3d(g.vertices[j*3+0], g.vertices[j*3+1], g.vertices[j*3+2]);
                }
            }
            glEnd();
        }
        glfwSwapBuffers(win);

        glfwPollEvents();
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