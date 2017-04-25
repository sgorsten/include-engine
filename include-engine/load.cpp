#include "load.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

image generate_single_color_image(const byte4 & color)
{
    auto p = std::malloc(sizeof(color));
    if(!p) throw std::bad_alloc();
    memcpy(p, &color, sizeof(color));
    return image{1, 1, std::unique_ptr<void, std_free_deleter>(p)};
}

image load_image(const char * filename) 
{ 
    int x, y;
    auto p = stbi_load(filename, &x, &y, nullptr, 4);
    if(!p) throw std::runtime_error(std::string("failed to load ") + filename);
    return image{x, y, std::unique_ptr<void, std_free_deleter>(p)};
}

mesh compute_tangent_basis(mesh && m)
{
    for(auto & v : m.vertices) v.tangent = v.bitangent = {};
    for(auto t : m.triangles)
    {
        auto & v0 = m.vertices[t.x], & v1 = m.vertices[t.y], & v2 = m.vertices[t.z];
        const float3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
        const float2 d1 = v1.texcoord - v0.texcoord, d2 = v2.texcoord - v0.texcoord;
        const float3 dpds = float3(d2.y * e1.x - d1.y * e2.x, d2.y * e1.y - d1.y * e2.y, d2.y * e1.z - d1.y * e2.z) / cross(d1, d2);
        const float3 dpdt = float3(d1.x * e2.x - d2.x * e1.x, d1.x * e2.y - d2.x * e1.y, d1.x * e2.z - d2.x * e1.z) / cross(d1, d2);
        v0.tangent += dpds; v1.tangent += dpds; v2.tangent += dpds;
        v0.bitangent += dpdt; v1.bitangent += dpdt; v2.bitangent += dpdt;
    }
    for(auto & v : m.vertices)
    {
        v.tangent = normalize(v.tangent);
        v.bitangent = normalize(v.bitangent);
    }
    return m;
}

mesh generate_box_mesh(const float3 & a, const float3 & b)
{
    return compute_tangent_basis({{
        {{a.x,a.y,a.z}, {1,1,1}, {-1,0,0}, {0,0}}, {{a.x,b.y,a.z}, {1,1,1}, {-1,0,0}, {1,0}}, {{a.x,b.y,b.z}, {1,1,1}, {-1,0,0}, {1,1}}, {{a.x,a.y,b.z}, {1,1,1}, {-1,0,0}, {0,1}},
        {{b.x,b.y,a.z}, {1,1,1}, {+1,0,0}, {0,0}}, {{b.x,a.y,a.z}, {1,1,1}, {+1,0,0}, {1,0}}, {{b.x,a.y,b.z}, {1,1,1}, {+1,0,0}, {1,1}}, {{b.x,b.y,b.z}, {1,1,1}, {+1,0,0}, {0,1}},
        {{a.x,a.y,a.z}, {1,1,1}, {0,-1,0}, {0,0}}, {{a.x,a.y,b.z}, {1,1,1}, {0,-1,0}, {1,0}}, {{b.x,a.y,b.z}, {1,1,1}, {0,-1,0}, {1,1}}, {{b.x,a.y,a.z}, {1,1,1}, {0,-1,0}, {0,1}},
        {{a.x,b.y,b.z}, {1,1,1}, {0,+1,0}, {0,0}}, {{a.x,b.y,a.z}, {1,1,1}, {0,+1,0}, {1,0}}, {{b.x,b.y,a.z}, {1,1,1}, {0,+1,0}, {1,1}}, {{b.x,b.y,b.z}, {1,1,1}, {0,+1,0}, {0,1}},
        {{a.x,a.y,a.z}, {1,1,1}, {0,0,-1}, {0,0}}, {{b.x,a.y,a.z}, {1,1,1}, {0,0,-1}, {1,0}}, {{b.x,b.y,a.z}, {1,1,1}, {0,0,-1}, {1,1}}, {{a.x,b.y,a.z}, {1,1,1}, {0,0,-1}, {0,1}},
        {{b.x,a.y,b.z}, {1,1,1}, {0,0,+1}, {0,0}}, {{a.x,a.y,b.z}, {1,1,1}, {0,0,+1}, {1,0}}, {{a.x,b.y,b.z}, {1,1,1}, {0,0,+1}, {1,1}}, {{b.x,b.y,b.z}, {1,1,1}, {0,0,+1}, {0,1}},
        }, {{0,1,2},{0,2,3},{4,5,6},{4,6,7},{8,9,10},{8,10,11},{12,13,14},{12,14,15},{16,17,18},{16,18,19},{20,21,22},{20,22,23}}, {}, {}, {{"", 0, 12}}
    });
}

mesh apply_vertex_color(mesh m, const float3 & color)
{
    for(auto & v : m.vertices) v.color = color;
    return m;
}

#include "fbx.h"
#include <iostream>

std::vector<mesh> load_meshes_from_fbx(coord_system target, const char * filename)
{
    std::ifstream in(filename, std::ifstream::binary);
    if(!in) throw std::runtime_error(std::string("unable to open ") + filename);
    auto meshes = fbx::load_meshes(fbx::ast::load(in));

    const coord_system fbx_coords {coord_axis::right, coord_axis::up, coord_axis::back};
    const auto xform = make_transform(fbx_coords, target);
    for(auto & m : meshes) m = compute_tangent_basis(transform(xform, std::move(m)));
    return meshes;
}

#include "../3rdparty/glslang/glslang/Public/ShaderLang.h"
#include "../3rdparty/glslang/StandAlone/ResourceLimits.h"
#include "../3rdparty/glslang/SPIRV/GlslangToSpv.h"

std::vector<char> load_text_file(const char * filename)
{
    FILE * f = fopen(filename, "r");
    if(!f) throw std::runtime_error(std::string("failed to open ") + filename);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buffer(len);
    buffer.resize(fread(buffer.data(), 1, buffer.size(), f));
    return buffer;
}

struct shader_compiler_impl : glslang::TShader::Includer
{
    struct result_text
    {
        std::vector<char> text;
        IncludeResult result;
        result_text(const std::string & header_name) : text{load_text_file(header_name.c_str())}, result{header_name, text.data(), text.size(), nullptr} {}
    };
    std::vector<std::unique_ptr<result_text>> results;

    IncludeResult * get_header(const std::string & name)
    {
        // Return previously loaded include file
        for(auto & r : results)
        {
            if(r->result.headerName == name)
            {
                return &r->result;
            }
        }

        // Otherwise attempt to load
        try
        {
            results.push_back(std::make_unique<result_text>(name));
            return &results.back()->result;
        }
        catch(const std::runtime_error &)
        {
            return nullptr; 
        }
    }

    // Implement glslang::TShader::Includer
    IncludeResult * includeSystem(const char * header_name, const char * includer_name, size_t inclusion_depth) override { return nullptr; }
    IncludeResult * includeLocal(const char * header_name, const char * includer_name, size_t inclusion_depth) override 
    { 
        std::string path {includer_name};
        size_t off = path.rfind('/');
        if(off != std::string::npos) path.resize(off+1);
        return get_header(path + header_name);
    }
    void releaseInclude(IncludeResult * result) override {}
};

shader_compiler::shader_compiler()
{
    glslang::InitializeProcess();
    impl = std::make_unique<shader_compiler_impl>();
}

shader_compiler::~shader_compiler()
{
    impl.reset();
    glslang::FinalizeProcess();
}

std::vector<uint32_t> shader_compiler::compile_glsl(VkShaderStageFlagBits stage, const char * filename)
{    
    glslang::TShader shader([stage]()
    {
        switch(stage)
        {
        case VK_SHADER_STAGE_VERTEX_BIT: return EShLangVertex;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return EShLangTessControl;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return EShLangTessEvaluation;
        case VK_SHADER_STAGE_GEOMETRY_BIT: return EShLangGeometry;
        case VK_SHADER_STAGE_FRAGMENT_BIT: return EShLangFragment;
        case VK_SHADER_STAGE_COMPUTE_BIT: return EShLangCompute;
        default: throw std::logic_error("bad stage");
        }
    }());    

    auto buffer = load_text_file(filename);
    const char * s = buffer.data();
    int l = static_cast<int>(buffer.size());
    shader.setStringsWithLengthsAndNames(&s, &l, &filename, 1);

    if(!shader.parse(&glslang::DefaultTBuiltInResource, 450, ENoProfile, false, false, static_cast<EShMessages>(EShMsgSpvRules|EShMsgVulkanRules), *impl))
    {
        throw std::runtime_error(std::string("GLSL compile failure: ") + shader.getInfoLog());
    }
    
    glslang::TProgram program;
    program.addShader(&shader);
    if(!program.link(EShMsgVulkanRules))
    {
        throw std::runtime_error(std::string("GLSL link failure: ") + program.getInfoLog());
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(shader.getStage()), spirv, nullptr);
    return spirv;
}

mesh load_mesh_from_obj(coord_system target, const char * filename)
{
    mesh m;
    std::map<std::string, uint32_t> vertex_map;
    std::vector<float3> vertices;
    std::vector<float2> texcoords;
    std::vector<float3> normals;
    auto find_vertex = [&](std::string && indices)
    {
        auto it = vertex_map.find(indices);
        if(it != vertex_map.end()) return it->second;

        const auto index = vertex_map[indices] = static_cast<uint32_t>(m.vertices.size());
        const bool no_texcoords = indices.find("//") != std::string::npos;
        for(auto & ch : indices) if(ch == '/') ch = ' ';
        int v {}, vt {}, vn {};
        std::istringstream ss(indices);
        ss >> v >> vt >> vn;
        if(no_texcoords) std::swap(vt, vn);
        mesh::vertex vertex {};
        if(v) vertex.position = vertices[v-1];
        if(vt) vertex.texcoord = texcoords[vt-1];
        if(vn) vertex.normal = normals[vn-1];
        vertex.color = {1,1,1};
        m.vertices.push_back(vertex);
        return index;
    };

    std::ifstream in(filename);
    std::string line, token;
    while(true)
    {
        if(!std::getline(in, line)) break;
        if(line.empty() || line.front() == '#') continue;
        std::istringstream ss(line);
        if(!(ss >> token)) continue;
        if(token == "v")
        {
            float3 vertex;
            if(!(ss >> vertex.x >> vertex.y >> vertex.z)) throw std::runtime_error("malformed vertex");
            vertices.push_back(vertex);
        }
        else if(token == "vt")
        {
            float2 texcoord;
            if(!(ss >> texcoord.x >> texcoord.y)) throw std::runtime_error("malformed vertex texture coords");
            texcoords.push_back({texcoord.x, 1-texcoord.y});
        }
        else if(token == "vn")
        {
            float3 normal;
            if(!(ss >> normal.x >> normal.y >> normal.z)) throw std::runtime_error("malformed vertex normal");
            normals.push_back(normal);
        }
        else if(token == "f")
        {
            std::vector<uint32_t> indices;
            while(true)
            {
                if(ss >> token) indices.push_back(find_vertex(std::move(token)));
                else break;
            }
            for(size_t i=2; i<indices.size(); ++i) m.triangles.push_back({indices[0], indices[i-1], indices[i]});
        }
        else if(token == "usemtl")
        {
            if(!m.materials.empty()) m.materials.back().num_triangles = m.triangles.size() - m.materials.back().first_triangle;
            ss >> token;
            m.materials.push_back({token, m.triangles.size(), 0});
        }
    }
    if(!m.materials.empty()) m.materials.back().num_triangles = m.triangles.size() - m.materials.back().first_triangle;
    const coord_system obj_coords {coord_axis::right, coord_axis::up, coord_axis::back};
    return compute_tangent_basis(transform(make_transform(obj_coords, target), m));
}