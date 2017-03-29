#include <fbx.h>
#include <opengl.h>
#include <data-types.h>
using namespace linalg::aliases;

#include <iostream>
#include <fstream>
#include <chrono>

#include <stb_image.h>

std::string load_text_file(const char * file)
{
    std::ifstream in(file);
    in.seekg(0, std::ifstream::end);
    std::string buffer(in.tellg(), 0);
    in.seekg(0, std::ifstream::beg);
    in.read(&buffer[0], buffer.size());
    while(!buffer.empty() && buffer.back() == '0') buffer.pop_back();
    return buffer;
}

int main() try
{
    std::ifstream in("assets/helmet-mesh.fbx", std::ifstream::binary);
    const auto doc = fbx::load(in);
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) std::cout << node << std::endl;

    auto models = load_models(doc);

    // Compute tangent space basis
    for(auto & m : models)
    {
        for(auto & g : m.geoms)
        {
            for(auto t : g.triangles)
            {
                auto & v0 = g.vertices[t.x], & v1 = g.vertices[t.y], & v2 = g.vertices[t.z];
                const float3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
                const float2 d1 = v1.texcoord - v0.texcoord, d2 = v2.texcoord - v0.texcoord;
                const float3 dpds = float3(d2.y * e1.x - d1.y * e2.x, d2.y * e1.y - d1.y * e2.y, d2.y * e1.z - d1.y * e2.z) / cross(d1, d2);
                const float3 dpdt = float3(d1.x * e2.x - d2.x * e1.x, d1.x * e2.y - d2.x * e1.y, d1.x * e2.z - d2.x * e1.z) / cross(d1, d2);
                v0.tangent += dpds; v1.tangent += dpds; v2.tangent += dpds;
                v0.bitangent += dpdt; v1.bitangent += dpdt; v2.bitangent += dpdt;
            }
            for(auto & v : g.vertices)
            {
                v.tangent = normalize(v.tangent);
                v.bitangent = normalize(v.bitangent);
            }
        }
    }

    glfwInit();
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SRGB_CAPABLE, 1);
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewInit();
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    struct mesh
    {
        float4x4 model_matrix;
        GLuint vertex_buffer;
        GLuint index_buffer;
        size_t index_count;
    };
    std::vector<mesh> meshes;
    for(auto & m : models)
    {
        for(auto & g : m.geoms)
        {
            mesh mesh;
            mesh.model_matrix = m.get_model_matrix();
            glCreateBuffers(1, &mesh.vertex_buffer);
            glNamedBufferStorage(mesh.vertex_buffer, g.vertices.size() * sizeof(fbx::geometry::vertex), g.vertices.data(), 0);
            glCreateBuffers(1, &mesh.index_buffer);
            glNamedBufferStorage(mesh.index_buffer, g.triangles.size() * sizeof(uint3), g.triangles.data(), 0);
            mesh.index_count = g.triangles.size() * 3;
            meshes.push_back(mesh);
        }
    }

    GLuint vaobj;
    glCreateVertexArrays(1, &vaobj);
    glVertexArrayAttribFormat(vaobj, 0, 3, GL_FLOAT, GL_FALSE, offsetof(fbx::geometry::vertex, position));
    glVertexArrayAttribFormat(vaobj, 1, 3, GL_FLOAT, GL_FALSE, offsetof(fbx::geometry::vertex, normal));
    glVertexArrayAttribFormat(vaobj, 2, 2, GL_FLOAT, GL_FALSE, offsetof(fbx::geometry::vertex, texcoord));
    glVertexArrayAttribFormat(vaobj, 3, 3, GL_FLOAT, GL_FALSE, offsetof(fbx::geometry::vertex, tangent));
    glVertexArrayAttribFormat(vaobj, 4, 3, GL_FLOAT, GL_FALSE, offsetof(fbx::geometry::vertex, bitangent));
    for(int i=0; i<5; ++i)
    {
        glEnableVertexArrayAttrib(vaobj, i);
        glVertexArrayAttribBinding(vaobj, i, 0);
    }

    auto tex_albedo = load_texture_2d(GL_SRGB8, "assets/helmet-albedo.jpg");
    auto tex_normal = load_texture_2d(GL_RGB8, "assets/helmet-normal.jpg");
    auto tex_metallic = load_texture_2d(GL_R8, "assets/helmet-metallic.jpg");
    auto tex_env = load_texture_cube(GL_SRGB8, "assets/posx.jpg", "assets/negx.jpg", "assets/posy.jpg", "assets/negy.jpg", "assets/posz.jpg", "assets/negz.jpg");

    auto program = link_program({
        compile_shader(GL_VERTEX_SHADER, load_text_file("assets/shader.vert")),
        compile_shader(GL_FRAGMENT_SHADER, load_text_file("assets/shader.frag"))
    });

    float3 camera_position {0,0,20};
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
        glEnable(GL_FRAMEBUFFER_SRGB);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                
        glEnable(GL_DEPTH_TEST);
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "u_view_proj_matrix"), 1, GL_FALSE, &view_proj_matrix.x.x);
        glUniform3fv(glGetUniformLocation(program, "u_eye_position"), 1, &camera_position.x);

        const float3  ambient_light {0.01f,0.01f,0.01f}, light_direction {normalize(float3{1,5,2})}, light_color {0.8f,0.7f,0.5f};
        glUniform3fv(glGetUniformLocation(program, "u_ambient_light"), 1, &ambient_light.x);
        glUniform3fv(glGetUniformLocation(program, "u_light_direction"), 1, &light_direction.x);
        glUniform3fv(glGetUniformLocation(program, "u_light_color"), 1, &light_color.x);

        for(auto & m : meshes)
        {
            glUniformMatrix4fv(glGetUniformLocation(program, "u_model_matrix"), 1, GL_FALSE, &m.model_matrix.x.x);
            glBindTextureUnit(0, tex_albedo->get_texture_name());
            glBindTextureUnit(1, tex_normal->get_texture_name());
            glBindTextureUnit(2, tex_metallic->get_texture_name());
            glBindTextureUnit(4, tex_env->get_texture_name());

            glBindVertexArray(vaobj);
            glVertexArrayVertexBuffer(vaobj, 0, m.vertex_buffer, 0, sizeof(fbx::geometry::vertex));
            glVertexArrayElementBuffer(vaobj, m.index_buffer);
            glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0);
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