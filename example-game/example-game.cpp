#include <fbx.h>
#include <opengl.h>
using namespace linalg::aliases;

#include <iostream>
#include <fstream>
#include <chrono>

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
    auto win = glfwCreateWindow(640, 480, "Example Game", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glewInit();

    auto program = link_program({
        compile_shader(GL_VERTEX_SHADER, R"(#version 450
            uniform mat4 u_model_matrix;
            uniform mat4 u_view_proj_matrix;
            layout(location = 0) in vec3 v_position;
            layout(location = 1) in vec3 v_normal;
            layout(location = 2) in vec2 v_texcoord;
            layout(location = 3) in vec3 v_tangent;
            layout(location = 4) in vec3 v_bitangent;
            out vec3 position;
            out vec3 normal;
            out vec2 texcoord;
            out vec3 tangent;
            out vec3 bitangent;
            void main() 
            { 
                position = (u_model_matrix * vec4(v_position, 1)).xyz;
                normal = normalize((u_model_matrix * vec4(v_normal, 0)).xyz);
                texcoord = v_texcoord;
                tangent = normalize((u_model_matrix * vec4(v_tangent, 0)).xyz);
                bitangent = normalize((u_model_matrix * vec4(v_bitangent, 0)).xyz);
                gl_Position = u_view_proj_matrix * vec4(position, 1); 
            }
        )"),
        compile_shader(GL_FRAGMENT_SHADER, R"(#version 450
            uniform vec3 u_eye_position;
            uniform vec3 u_ambient_light;
            uniform vec3 u_light_direction;
            uniform vec3 u_light_color;
            layout(binding = 0) uniform sampler2D u_albedo;
            layout(binding = 1) uniform sampler2D u_normal;
            in vec3 position;
            in vec3 normal;
            in vec2 texcoord;
            in vec3 tangent;
            in vec3 bitangent;
            layout(location = 0) out vec4 f_color;
            void main()
            {
                vec3 eye_vec = normalize(u_eye_position - position);
                vec3 albedo = texture(u_albedo, texcoord).rgb;
                vec3 tan_normal = normalize(texture(u_normal, texcoord).xyz*2-1);
                vec3 normal_vec = normalize(normalize(tangent)*tan_normal.x + normalize(bitangent)*tan_normal.y + normalize(normal)*tan_normal.z);

                vec3 light = u_ambient_light;

                vec3 light_vec = u_light_direction;
                light += albedo * u_light_color * max(dot(normal_vec, light_vec), 0);
                vec3 half_vec = normalize(light_vec + eye_vec);                
                light += u_light_color * pow(max(dot(normal_vec, half_vec), 0), 128);

                f_color = vec4(light,1);
            }
        )")
    });

    auto tex_albedo = load_texture("assets/helmet-albedo.jpg");
    auto tex_normal = load_texture("assets/helmet-normal.jpg");

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
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                
        glEnable(GL_DEPTH_TEST);
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "u_view_proj_matrix"), 1, GL_FALSE, &view_proj_matrix.x.x);
        glUniform3fv(glGetUniformLocation(program, "u_eye_position"), 1, &camera_position.x);

        const float3  ambient_light {0.1f,0.1f,0.1f}, light_direction {normalize(float3{1,5,2})}, light_color {0.8f,0.7f,0.5f};
        glUniform3fv(glGetUniformLocation(program, "u_ambient_light"), 1, &ambient_light.x);
        glUniform3fv(glGetUniformLocation(program, "u_light_direction"), 1, &light_direction.x);
        glUniform3fv(glGetUniformLocation(program, "u_light_color"), 1, &light_color.x);

        for(auto & m : models)
        {
            const auto model_matrix = m.get_model_matrix();
            glUniformMatrix4fv(glGetUniformLocation(program, "u_model_matrix"), 1, GL_FALSE, &model_matrix.x.x);
            glBindTextureUnit(0, tex_albedo);
            glBindTextureUnit(1, tex_normal);

            for(auto & g : m.geoms)
            {
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->position);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->normal);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->texcoord);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->tangent);
                glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(fbx::geometry::vertex), &g.vertices.data()->bitangent);
                for(GLuint i : {0,1,2,3,4}) glEnableVertexAttribArray(i);
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