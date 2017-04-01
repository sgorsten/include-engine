#include "load.h"
#include <fstream>
#include <algorithm>

std::vector<uint32_t> load_spirv_binary(const char * path)
{
    FILE * f = fopen(path, "rb");
    if(!f) throw std::runtime_error(std::string("failed to open ") + path);
    fseek(f, 0, SEEK_END);
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> words(len/4);
    if(fread(words.data(), sizeof(uint32_t), words.size(), f) != words.size()) throw std::runtime_error(std::string("failed to read ") + path);
    fclose(f);
    return words;
}

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

image load_image(const char * filename) 
{ 
    int x, y;
    auto p = stbi_load(filename, &x, &y, nullptr, 4);
    if(!p) throw std::runtime_error(std::string("failed to load ") + filename);
    return image{x, y, std::unique_ptr<void, std_free_deleter>(p)};
}

#include "fbx.h"

std::vector<mesh> load_meshes_from_fbx(const char * filename)
{
    std::ifstream in(filename, std::ifstream::binary);
    const auto doc = fbx::load(in);
    //std::cout << "FBX Version " << doc.version << std::endl;
    //for(auto & node : doc.nodes) std::cout << node << std::endl;
    auto models = load_models(doc);

    std::vector<mesh> meshes;
    for(auto & m : models) 
    {
        auto model_matrix = mul(scaling_matrix(float3{1,-1,-1}), m.get_model_matrix());
        auto normal_matrix = inverse(transpose(model_matrix));
        for(auto & g : m.geoms)
        {
            // Produce mesh by transforming FbxGeometry by FbxModel transform
            mesh mesh;
            for(auto & v : g.vertices)
            {
                mesh.vertices.push_back({mul(model_matrix, float4{v.position,1}).xyz(), mul(normal_matrix, float4{v.normal,0}).xyz(), v.texcoord});
            }
            mesh.triangles = g.triangles;

            // Compute tangents and bitangents
            for(auto t : mesh.triangles)
            {
                auto & v0 = mesh.vertices[t.x], & v1 = mesh.vertices[t.y], & v2 = mesh.vertices[t.z];
                const float3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
                const float2 d1 = v1.texcoord - v0.texcoord, d2 = v2.texcoord - v0.texcoord;
                const float3 dpds = float3(d2.y * e1.x - d1.y * e2.x, d2.y * e1.y - d1.y * e2.y, d2.y * e1.z - d1.y * e2.z) / cross(d1, d2);
                const float3 dpdt = float3(d1.x * e2.x - d2.x * e1.x, d1.x * e2.y - d2.x * e1.y, d1.x * e2.z - d2.x * e1.z) / cross(d1, d2);
                v0.tangent += dpds; v1.tangent += dpds; v2.tangent += dpds;
                v0.bitangent += dpdt; v1.bitangent += dpdt; v2.bitangent += dpdt;
            }
            for(auto & v : mesh.vertices)
            {
                v.tangent = normalize(v.tangent);
                v.bitangent = normalize(v.bitangent);
            }

            meshes.push_back(std::move(mesh));
        }
    }
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

std::vector<uint32_t> shader_compiler::compile_glsl(const char * filename)
{
    auto buffer = load_text_file(filename);

    const size_t len = strlen(filename);
    EShLanguage lang;
    if(filename[len-4] == 'v') lang = EShLanguage::EShLangVertex;
    else if(filename[len-4] == 'f') lang = EShLanguage::EShLangFragment;
    else throw std::runtime_error("unknown shader stage");
    
    glslang::TShader shader(lang);
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
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, nullptr);
    return spirv;
}