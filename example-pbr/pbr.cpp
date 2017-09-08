#include "pbr.h"
#include <sstream>

GLuint compile_shader(GLenum type, std::initializer_list<std::string_view> sources)
{
    constexpr size_t max_sources = 16;
    const GLchar * strings[max_sources] {};
    GLint lengths[max_sources] {};
    GLsizei count = 0;
    for(auto source : sources)
    {
        strings[count] = source.data();
        lengths[count] = source.size();
        if(++count == max_sources) break;
    }

    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, count, strings, lengths);
    glCompileShader(shader);

    GLint compile_status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if(compile_status != GL_TRUE)
    {
        GLint info_log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);

        std::vector<GLchar> info_log(info_log_length);
        glGetShaderInfoLog(shader, info_log.size(), nullptr, info_log.data());
        glDeleteShader(shader);

        std::ostringstream ss;
        ss << "compile_shader(...) failed with log:\n" << info_log.data() << "and sources:\n";
        for(auto source : sources) ss << source;
        throw std::runtime_error(ss.str());
    }

    return shader;
}

gl_program::gl_program(std::initializer_list<GLuint> shader_stages) : gl_program{}
{
    program = glCreateProgram();
    for(auto shader : shader_stages) glAttachShader(program, shader);
    glLinkProgram(program);

    GLint link_status;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if(link_status != GL_TRUE)
    {
        GLint info_log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);

        std::vector<GLchar> info_log(info_log_length);
        glGetProgramInfoLog(program, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error(info_log.data());
    }
}

gl_program::~gl_program() { if(program) glDeleteProgram(program); }

void gl_program::use() const { glUseProgram(program); }
std::optional<int> gl_program::get_uniform_location(const char * name) const 
{ 
    const GLint location = glGetUniformLocation(program, name);
    if(location < 0) return std::nullopt;
    return location;
}

void gl_program::bind_texture(GLint location, GLuint texture) const { GLint binding; glGetUniformiv(program, location, &binding); glBindTextureUnit(binding, texture); }

void gl_program::uniform(GLint location, float scalar) { glProgramUniform1f(program, location, scalar); }
void gl_program::uniform(GLint location, const float2 & vec) { glProgramUniform2fv(program, location, 1, &vec[0]); }
void gl_program::uniform(GLint location, const float3 & vec) { glProgramUniform3fv(program, location, 1, &vec[0]); }
void gl_program::uniform(GLint location, const float4 & vec) { glProgramUniform4fv(program, location, 1, &vec[0]); }
void gl_program::uniform(GLint location, const float4x4 & mat) { glProgramUniformMatrix4fv(program, location, 1, GL_FALSE, &mat[0][0]); }

std::string_view preamble = R"(#version 450
const float pi = 3.14159265359, tau = 6.28318530718;
float dotp(vec3 a, vec3 b) { return max(dot(a,b),0); }
float pow2(float x) { return x*x; }
float length2(vec3 v) { return dot(v,v); }

// Our physically based lighting equations use the following common terminology
// N - normal vector, unit vector perpendicular to the surface
// V - view vector, unit vector pointing from the surface towards the viewer
// L - light vector, unit vector pointing from the surface towards the light source
// H - half-angle vector, unit vector halfway between V and L
// R - reflection vector, V mirrored about N
// F0 - base reflectance of the surface
// alpha - common measure of surface roughness
float roughness_to_alpha(float roughness) { return roughness*roughness; }
float trowbridge_reitz_ggx(vec3 N, vec3 H, float alpha) { return alpha*alpha / (pi * pow2(dotp(N,H)*dotp(N,H)*(alpha*alpha-1) + 1)); }
float geometry_schlick_ggx(vec3 N, vec3 V, float k) { return dotp(N,V) / (dotp(N,V)*(1-k) + k); }
float geometry_smith(vec3 N, vec3 V, vec3 L, float k) { return geometry_schlick_ggx(N, L, k) * geometry_schlick_ggx(N, V, k); }
vec3 fresnel_schlick(vec3 V, vec3 H, vec3 F0) { return F0 + (1-F0) * pow(1-dotp(V,H), 5); }
vec3 cook_torrance(vec3 N, vec3 V, vec3 L, vec3 H, vec3 albedo, vec3 F0, float alpha, float metalness)
{
    const float D       = trowbridge_reitz_ggx(N, H, alpha);
    const float G       = geometry_smith(N, V, L, (alpha+1)*(alpha+1)/8);
    const vec3 F        = fresnel_schlick(V, H, F0);
    const vec3 diffuse  = (1-F) * (1-metalness) * albedo/pi;
    const vec3 specular = (D * G * F) / (4 * dotp(N,V) * dotp(N,L) + 0.001);  
    return (diffuse + specular) * dotp(N,L);
}

vec3 spherical(float phi, float cos_theta, float sin_theta) { return vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta); };
vec3 spherical(float phi, float theta) { return spherical(phi, cos(theta), sin(theta)); }
mat3 tangent_basis(vec3 z_direction)
{
    const vec3 z = normalize(z_direction);    
    const vec3 x = normalize(cross(abs(z.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0), z));
    const vec3 y = cross(z, x);
    return mat3(x, y, z);
}
)";

std::string_view pbr_lighting = R"(
// This function computes the full lighting to apply to a single fragment
uniform vec3 u_eye_position;
layout(binding=0) uniform sampler2D u_brdf_integration_map;
layout(binding=1) uniform samplerCube u_irradiance_map;
layout(binding=2) uniform samplerCube u_reflectance_map;
const float MAX_REFLECTANCE_LOD = 4.0;
vec3 compute_lighting(vec3 position, vec3 normal, vec3 albedo, float roughness, float metalness, float ambient_occlusion)
{
    // Compute common terms of lighting equations
    const vec3 N = normalize(normal);
    const vec3 V = normalize(u_eye_position - position);
    const vec3 R = reflect(-V, N);
    const vec3 F0 = mix(vec3(0.04), albedo, metalness);
    const float alpha = roughness_to_alpha(roughness);

    // Initialize our light accumulator
    vec3 light = vec3(0,0,0);

    // Add contribution from indirect lights
    {
        vec2 brdf = texture(u_brdf_integration_map, vec2(dotp(N,V), roughness)).xy;
        vec3 F    = F0 + max(1-F0-roughness, 0) * pow(1-dotp(N,V), 5);
        vec3 spec = (F * brdf.x + brdf.y) * textureLod(u_reflectance_map, R, roughness * MAX_REFLECTANCE_LOD).rgb;
        vec3 diff = (1-F) * (1-metalness) * albedo * texture(u_irradiance_map, N).rgb;
        light     += (diff + spec) * ambient_occlusion; 
    }

    // Add contributions from direct lights
    const vec3 light_positions[4] = {vec3(-3, -3, 8), vec3(3, -3, 8), vec3(3, 3, 8), vec3(-3, 3, 8)};
    const vec3 light_colors[4] = {vec3(23.47, 21.31, 20.79), vec3(23.47, 21.31, 20.79), vec3(23.47, 21.31, 20.79), vec3(23.47, 21.31, 20.79)};
    for(int i=0; i<4; ++i)
    {
        // Evaluate light vector, half-angle vector, and radiance of light source at the current distance
        const vec3 L = normalize(light_positions[i] - position);
        const vec3 H = normalize(V + L);
        const vec3 radiance = light_colors[i] / length2(light_positions[i] - position);
        light += radiance * cook_torrance(N, V, L, H, albedo, F0, alpha, metalness);
    }
    return light;
}
)";

constexpr char skybox_vert_shader_source[] = R"(
uniform mat4 u_view_proj_matrix;
layout(location=0) in vec3 v_direction;
layout(location=0) out vec3 direction;
void main()
{
    direction   = v_direction;
    gl_Position = u_view_proj_matrix * vec4(direction,1);
})";

constexpr char spheremap_skybox_frag_shader_source[] = R"(
uniform sampler2D u_texture;
layout(location=0) in vec3 direction;
layout(location=0) out vec4 f_color;
vec2 compute_spherical_texcoords(vec3 direction)
{
    return vec2(atan(direction.x, direction.z)*0.1591549, asin(direction.y)*0.3183099 + 0.5);
}
void main()
{
    f_color = texture(u_texture, compute_spherical_texcoords(normalize(direction)));
})";

constexpr char cubemap_skybox_frag_shader_source[] = R"(
uniform samplerCube u_texture;
layout(location=0) in vec3 direction;
layout(location=0) out vec4 f_color;
void main()
{
    f_color = textureLod(u_texture, direction, 1.2);
})";

constexpr char irradiance_frag_shader_source[] = R"(
uniform samplerCube u_texture;
layout(location=0) in vec3 direction;
layout(location=0) out vec4 f_color;
void main()
{
    const mat3 basis = tangent_basis(direction);

    vec3 irradiance = vec3(0,0,0);
    float num_samples = 0; 
    for(float phi=0; phi<tau; phi+=0.01)
    {
        for(float theta=0; theta<tau/4; theta+=0.01)
        {
            // Sample irradiance from the source texture, and weight by the sampling area
            vec3 L = basis * spherical(phi, theta);
            irradiance += texture(u_texture, L).rgb * cos(theta) * sin(theta);
            ++num_samples;
        }
    }
    f_color = vec4(irradiance*(pi/num_samples), 1);
})";

constexpr char importance_sample_ggx[] = R"(  
vec3 importance_sample_ggx(float alpha, uint i, uint n)
{
    // Phi is distributed uniformly over the integration range
    const float phi = i*tau/n;

    // Theta is importance-sampled using the Van Der Corpus sequence
    i = (i << 16u) | (i >> 16u);
    i = ((i & 0x55555555u) << 1u) | ((i & 0xAAAAAAAAu) >> 1u);
    i = ((i & 0x33333333u) << 2u) | ((i & 0xCCCCCCCCu) >> 2u);
    i = ((i & 0x0F0F0F0Fu) << 4u) | ((i & 0xF0F0F0F0u) >> 4u);
    i = ((i & 0x00FF00FFu) << 8u) | ((i & 0xFF00FF00u) >> 8u);
    float radical_inverse = i * 2.3283064365386963e-10; // Divide by 0x100000000
    float cos_theta = sqrt((1 - radical_inverse) / ((alpha*alpha-1)*radical_inverse + 1));
    return spherical(phi, cos_theta, sqrt(1 - cos_theta*cos_theta));
}
)";

constexpr char reflectance_frag_shader_source[] = R"(
uniform samplerCube u_texture;
uniform float u_roughness;
layout(location=0) in vec3 direction;
layout(location=0) out vec4 f_color;

const int sample_count = 1024;
void main()
{
    // As we are evaluating base reflectance, both the normal and view vectors are equal to our sampling direction
    const vec3 N = normalize(direction), V = N;
    const mat3 basis = tangent_basis(N);
    const float alpha = roughness_to_alpha(u_roughness);

    // Precompute the average solid angle of a cube map texel
    const int cube_width = textureSize(u_texture, 0).x;
    const float texel_solid_angle = pi*4 / (6*cube_width*cube_width);

    vec3 sum_color = vec3(0,0,0);
    float sum_weight = 0;     
    for(int i=0; i<sample_count; ++i)
    {
        // For the desired roughness, sample possible half-angle vectors, and compute the lighting vector from them
        const vec3 H = basis * importance_sample_ggx(alpha, i, sample_count);
        const vec3 L = normalize(2*dot(V,H)*H - V);
        if(dot(N, L) <= 0) continue;

        // Compute the mip-level at which to sample
        const float D = trowbridge_reitz_ggx(N, H, alpha);
        const float pdf = D*dotp(N,H) / (4*dotp(V,H)) + 0.0001; 
        const float sample_solid_angle = 1 / (sample_count * pdf + 0.0001);
        const float mip_level = alpha > 0 ? log2(sample_solid_angle / texel_solid_angle)/2 : 0;

        // Sample the environment map according to the lighting direction, and weight the resulting contribution by N dot L
        sum_color += textureLod(u_texture, L, mip_level).rgb * dot(N, L);
        sum_weight += dot(N, L);
    }

    f_color = vec4(sum_color/sum_weight, 1);
})";

constexpr char fullscreen_pass_vert_shader_source[] = R"(
layout(location=0) in vec2 v_position;
layout(location=1) in vec2 v_texcoords;
layout(location=0) out vec2 texcoords;
void main()
{
    texcoords = v_texcoords;
    gl_Position = vec4(v_position,0,1);
})";

constexpr char brdf_integration_frag_shader_source[] = R"(
layout(location=0) in vec2 texcoords;
layout(location=0) out vec4 f_color;

const int sample_count = 1024;
vec2 integrate_brdf(float n_dot_v, float alpha)
{
    // Without loss of generality, evaluate the case where the normal is aligned with the z-axis and the viewing direction is in the xz-plane
    const vec3 N = vec3(0,0,1);
    const vec3 V = vec3(sqrt(1 - n_dot_v*n_dot_v), 0, n_dot_v);

    vec2 result = vec2(0,0);    
    for(int i=0; i<sample_count; ++i)
    {
        // For the desired roughness, sample possible half-angle vectors, and compute the lighting vector from them
        const vec3 H = importance_sample_ggx(alpha, i, sample_count);
        const vec3 L = normalize(2 * dot(V, H) * H - V);
        if(dot(N, L) <= 0) continue;

        // Integrate results
        const float Fc = pow(1 - dotp(V,H), 5);
        const float G = geometry_smith(N, V, L, alpha*alpha/2);
        const float G_Vis = (G * dotp(V,H)) / (dotp(N,H) * n_dot_v);
        result.x += (1 - Fc) * G_Vis;
        result.y += Fc * G_Vis;
    }
    return result/sample_count;
}
void main() 
{
    f_color = vec4(integrate_brdf(texcoords.x, roughness_to_alpha(texcoords.y)), 0, 1);
})";

pbr_tools::pbr_tools()
{
    GLuint skybox_vs           = compile_shader(GL_VERTEX_SHADER, {preamble, skybox_vert_shader_source});
    GLuint fullscreen_pass_vs  = compile_shader(GL_VERTEX_SHADER, {preamble, fullscreen_pass_vert_shader_source});
    spheremap_skybox_prog      = {skybox_vs, compile_shader(GL_FRAGMENT_SHADER, {preamble, spheremap_skybox_frag_shader_source})};
    cubemap_skybox_prog        = {skybox_vs, compile_shader(GL_FRAGMENT_SHADER, {preamble, cubemap_skybox_frag_shader_source})};
    irradiance_prog            = {skybox_vs, compile_shader(GL_FRAGMENT_SHADER, {preamble, irradiance_frag_shader_source})};
    reflectance_prog           = {skybox_vs, compile_shader(GL_FRAGMENT_SHADER, {preamble, importance_sample_ggx, reflectance_frag_shader_source})};
    brdf_integration_prog      = {fullscreen_pass_vs, compile_shader(GL_FRAGMENT_SHADER, {preamble, importance_sample_ggx, brdf_integration_frag_shader_source})};
}

template<class F> GLuint render_cubemap(GLsizei levels, GLenum internal_format, GLsizei width, F draw_face)
{
    // If user passes levels == 0, let the user draw level 1 and have OpenGL generate a mip chain
    GLsizei user_levels = levels ? levels : 1;

    GLuint cubemap;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &cubemap);
    glTextureStorage2D(cubemap, levels ? levels : 1+static_cast<GLsizei>(std::ceil(std::log2(width))), internal_format, width, width);
    glTextureParameteri(cubemap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cubemap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cubemap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cubemap, GL_TEXTURE_MIN_FILTER, levels == 1 ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(cubemap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo;
    glCreateFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    for(GLint mip=0; mip<user_levels; ++mip)
    {
        glViewport(0, 0, width, width);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, cubemap, mip); draw_face(float4x4{{0,0,+1,0},{0,+1,0,0},{-1,0,0,0},{0,0,0,1}}, mip);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, cubemap, mip); draw_face(float4x4{{0,0,-1,0},{0,+1,0,0},{+1,0,0,0},{0,0,0,1}}, mip);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, cubemap, mip); draw_face(float4x4{{+1,0,0,0},{0,0,+1,0},{0,-1,0,0},{0,0,0,1}}, mip);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, cubemap, mip); draw_face(float4x4{{+1,0,0,0},{0,0,-1,0},{0,+1,0,0},{0,0,0,1}}, mip);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, cubemap, mip); draw_face(float4x4{{+1,0,0,0},{0,+1,0,0},{0,0,+1,0},{0,0,0,1}}, mip);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, cubemap, mip); draw_face(float4x4{{-1,0,0,0},{0,+1,0,0},{0,0,-1,0},{0,0,0,1}}, mip);
        width = std::max(width/2, 1);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    if(levels == 0) glGenerateTextureMipmap(cubemap);
    return cubemap; 
}

constexpr float3 skybox_verts[]
{
    {-1,-1,-1}, {-1,+1,-1}, {-1,+1,+1}, {-1,-1,+1},
    {+1,-1,-1}, {+1,-1,+1}, {+1,+1,+1}, {+1,+1,-1},
    {-1,-1,-1}, {-1,-1,+1}, {+1,-1,+1}, {+1,-1,-1},
    {-1,+1,-1}, {+1,+1,-1}, {+1,+1,+1}, {-1,+1,+1},
    {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
    {-1,-1,+1}, {-1,+1,+1}, {+1,+1,+1}, {+1,-1,+1}
};

GLuint pbr_tools::convert_spheremap_to_cubemap(GLenum internal_format, GLsizei width, GLuint spheremap) const
{
    spheremap_skybox_prog.bind_texture("u_texture", spheremap);
    spheremap_skybox_prog.use();
    return render_cubemap(0, internal_format, width, [&](const float4x4 & view_proj_matrix, int mip)
    {        
        spheremap_skybox_prog.uniform("u_view_proj_matrix", view_proj_matrix);
        glBegin(GL_QUADS);
        for(auto & v : skybox_verts) glVertex3fv(&v[0]);
        glEnd();
    });
}

GLuint pbr_tools::compute_irradiance_map(GLuint cubemap) const
{
    irradiance_prog.bind_texture("u_texture", cubemap);
    irradiance_prog.use();
    return render_cubemap(1, GL_RGB16F, 32, [&](const float4x4 & view_proj_matrix, int mip)
    {
        irradiance_prog.uniform("u_view_proj_matrix", view_proj_matrix);
        glBegin(GL_QUADS);
        for(auto & v : skybox_verts) glVertex3fv(&v[0]);
        glEnd();
    });
}

GLuint pbr_tools::compute_reflectance_map(GLuint cubemap) const
{
    reflectance_prog.bind_texture("u_texture", cubemap);
    reflectance_prog.use();;
    return render_cubemap(5, GL_RGB16F, 128, [&](const float4x4 & view_proj_matrix, int mip)
    {
        reflectance_prog.uniform("u_view_proj_matrix", view_proj_matrix);
        reflectance_prog.uniform("u_roughness", mip/4.0f);
        glBegin(GL_QUADS);
        for(auto & v : skybox_verts) glVertex3fv(&v[0]);
        glEnd();
    });
}

GLuint pbr_tools::compute_brdf_integration_map() const
{
    GLuint brdf_integration_map;
    glCreateTextures(GL_TEXTURE_2D, 1, &brdf_integration_map);
    glTextureStorage2D(brdf_integration_map, 1, GL_RG16F, 512, 512);
    glTextureParameteri(brdf_integration_map, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(brdf_integration_map, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(brdf_integration_map, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(brdf_integration_map, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo;
    glCreateFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdf_integration_map, 0);
    glViewport(0,0,512,512);

    brdf_integration_prog.use();
    glBegin(GL_QUADS);
    glVertexAttrib2f(1, 0, 0); glVertex2f(-1, -1);
    glVertexAttrib2f(1, 0, 1); glVertex2f(-1, +1);
    glVertexAttrib2f(1, 1, 1); glVertex2f(+1, +1);
    glVertexAttrib2f(1, 1, 0); glVertex2f(+1, -1);
    glEnd();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    return brdf_integration_map;
}

void pbr_tools::draw_skybox(GLuint cubemap, const float4x4 & skybox_view_proj_matrix) const
{
    cubemap_skybox_prog.bind_texture("u_texture", cubemap);
    cubemap_skybox_prog.uniform("u_view_proj_matrix", skybox_view_proj_matrix);
    cubemap_skybox_prog.use();
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for(auto & v : skybox_verts) glVertex3fv(&v[0]);
    glEnd();
    glDepthMask(GL_TRUE);
}
