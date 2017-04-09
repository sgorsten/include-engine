#include "load.h"
#include <fstream>
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
        {{a.x,a.y,a.z}, {-1,0,0}, {0,0}}, {{a.x,b.y,a.z}, {-1,0,0}, {1,0}}, {{a.x,b.y,b.z}, {-1,0,0}, {1,1}}, {{a.x,a.y,b.z}, {-1,0,0}, {0,1}},
        {{b.x,b.y,a.z}, {+1,0,0}, {0,0}}, {{b.x,a.y,a.z}, {+1,0,0}, {1,0}}, {{b.x,a.y,b.z}, {+1,0,0}, {1,1}}, {{b.x,b.y,b.z}, {+1,0,0}, {0,1}},
        {{a.x,a.y,a.z}, {0,-1,0}, {0,0}}, {{a.x,a.y,b.z}, {0,-1,0}, {1,0}}, {{b.x,a.y,b.z}, {0,-1,0}, {1,1}}, {{b.x,a.y,a.z}, {0,-1,0}, {0,1}},
        {{a.x,b.y,b.z}, {0,+1,0}, {0,0}}, {{a.x,b.y,a.z}, {0,+1,0}, {1,0}}, {{b.x,b.y,a.z}, {0,+1,0}, {1,1}}, {{b.x,b.y,b.z}, {0,+1,0}, {0,1}},
        {{a.x,a.y,a.z}, {0,0,-1}, {0,0}}, {{b.x,a.y,a.z}, {0,0,-1}, {1,0}}, {{b.x,b.y,a.z}, {0,0,-1}, {1,1}}, {{a.x,b.y,a.z}, {0,0,-1}, {0,1}},
        {{b.x,a.y,b.z}, {0,0,+1}, {0,0}}, {{a.x,a.y,b.z}, {0,0,+1}, {1,0}}, {{a.x,b.y,b.z}, {0,0,+1}, {1,1}}, {{b.x,b.y,b.z}, {0,0,+1}, {0,1}},
    }, {{0,1,2},{0,2,3},{4,5,6},{4,6,7},{8,9,10},{8,10,11},{12,13,14},{12,14,15},{16,17,18},{16,18,19},{20,21,22},{20,22,23}}});
}

#include "fbx.h"
#include <iostream>

std::vector<mesh> load_meshes_from_fbx(const char * filename)
{
    std::ifstream in(filename, std::ifstream::binary);
    auto meshes = fbx::load_meshes(fbx::ast::load(in));
    for(auto & m : meshes) m = compute_tangent_basis(std::move(m));
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
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buffer(len);
    len = fread(buffer.data(), 1, buffer.size(), f);
    buffer.resize(len);
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
    int l = buffer.size();
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