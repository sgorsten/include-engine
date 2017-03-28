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

GLuint compile_shader(GLenum type, const char * source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        std::string log(length, ' ');
        glGetShaderInfoLog(shader, length, nullptr, &log[0]);
        glDeleteShader(shader);
        throw std::runtime_error("compile error " + log);
    }

    return shader;
}

GLuint link_program(std::initializer_list<GLuint> shaders)
{
    GLuint program = glCreateProgram();
    for(auto shader : shaders) glAttachShader(program, shader);
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        std::string log(length, ' ');
        glGetProgramInfoLog(program, length, nullptr, &log[0]);
        glDeleteProgram(program);
        throw std::runtime_error("link error " + log);
    }

    return program;
}

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

    auto program = link_program({
        compile_shader(GL_VERTEX_SHADER, R"(#version 450
            uniform mat4 u_model_matrix;
            uniform mat4 u_view_proj_matrix;
            layout(location = 0) in vec3 v_position;
            layout(location = 1) in vec3 v_normal;
            layout(location = 2) in vec2 v_texcoord;
            out vec3 position;
            out vec3 normal;
            out vec2 texcoord;
            void main() 
            { 
                position = (u_model_matrix * vec4(v_position, 1)).xyz;
                normal = normalize((u_model_matrix * vec4(v_normal, 0)).xyz);
                texcoord = v_texcoord;
                gl_Position = u_view_proj_matrix * vec4(position, 1); 
            }
        )"),
        compile_shader(GL_FRAGMENT_SHADER, R"(#version 450
            uniform sampler2D u_albedo;
            in vec3 position;
            in vec3 normal;
            in vec2 texcoord;
            layout(location = 0) out vec4 f_color;
            void main()
            {
                vec3 normal_vec = normalize(normal);
                vec3 light_vec = normalize(vec3(1,5,2));
                float diffuse_factor = max(dot(normal_vec, light_vec), 0);                
                f_color = texture(u_albedo, texcoord) * diffuse_factor;
            }
        )")
    });

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
        const auto view_proj_matrix = mul(proj_matrix, view_matrix);

        // Clear the viewport
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                
        glEnable(GL_DEPTH_TEST);
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "u_view_proj_matrix"), 1, GL_FALSE, &view_proj_matrix.x.x);

        for(auto & m : models)
        {
            const auto model_matrix = m.get_model_matrix();
            glUniformMatrix4fv(glGetUniformLocation(program, "u_model_matrix"), 1, GL_FALSE, &model_matrix.x.x);

            for(auto & g : m.geoms)
            {
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->position);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->normal);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->texcoord);
                for(GLuint i : {0,1,2}) glEnableVertexAttribArray(i);
                glDrawElements(GL_TRIANGLES, g.triangles.size()*3, GL_UNSIGNED_INT, g.triangles.data());
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