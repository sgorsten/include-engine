#include <fbx.h>
using namespace linalg::aliases;

#include <iostream>
#include <fstream>
#include <chrono>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#pragma comment(lib, "opengl32.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

int main() try
{
    std::ifstream in("assets/helmet-mesh.fbx", std::ifstream::binary);
    const auto doc = fbx::load(in);
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) std::cout << node << std::endl;

    auto models = load_models(doc);

    glfwInit();
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glewInit();

    int x, y;
    auto image = stbi_load("assets/helmet-albedo.jpg", &x, &y, nullptr, 3);
    if(!image) throw std::runtime_error("failed to load helmet-albedo.jpg");
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, x, y, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(image);

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
        const auto proj_matrix = linalg::perspective_matrix(1.0f, (float)width/height, 1.0f, 1000.0f);
        const auto view_matrix = inverse(pose_matrix(camera_orientation, camera_position));

        // Clear the viewport
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(&proj_matrix.x.x);

        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(&view_matrix.x.x);

        auto light_dir = normalize(float4{1,5,2,0});
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, &light_dir.x);
        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);

        for(auto & m : models)
        {
            const auto model_matrix = m.get_model_matrix();
            const auto model_view_matrix = mul(view_matrix, model_matrix);
            glLoadMatrixf(&model_view_matrix.x.x);

            for(auto & g : m.geoms)
            {
                glEnable(GL_TEXTURE_2D);
                glBegin(GL_TRIANGLES);
                for(auto & triangle : g.triangles)
                {
                    for(auto index : triangle) 
                    {
                        glTexCoord2fv(&g.vertices[index].texcoord.x);
                        glNormal3fv(&g.vertices[index].normal.x);
                        glVertex3fv(&g.vertices[index].position.x);
                    }
                }
                glEnd();
            }
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