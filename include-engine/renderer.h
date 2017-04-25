#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan.h" // For Vulkan API access
#include "load.h"   // For shader_compiler

// Scene rendering support
class scene_contract
{
    friend class scene_pipeline_layout;
    context & ctx;
    VkRenderPass render_pass;
    VkDescriptorSetLayout per_scene_layout; // Descriptors which are shared by the entire scene
    VkDescriptorSetLayout per_view_layout;  // Descriptors which vary per unique view into the scene
public:
    scene_contract(context & ctx, VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    ~scene_contract();

    VkDescriptorSetLayout get_per_scene_layout() const { return per_scene_layout; }
    VkDescriptorSetLayout get_per_view_layout() const { return per_view_layout; }
};

class scene_pipeline_layout
{
    std::shared_ptr<scene_contract> contract;
    VkDescriptorSetLayout per_object_layout;
    VkPipelineLayout pipeline_layout;
public:
    scene_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings);
    ~scene_pipeline_layout();

    VkDevice get_device() const { return contract->ctx.device; }
    VkRenderPass get_render_pass() const { return contract->render_pass; }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return per_object_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }
};

class vertex_format
{
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
public:
    vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes);

    VkPipelineVertexInputStateCreateInfo get_vertex_input_state() const;
    array_view<VkVertexInputBindingDescription> get_bindings() const { return bindings; }
    array_view<VkVertexInputAttributeDescription> get_attributes() const { return attributes; }
};

class shader
{
    context & ctx;
    VkShaderModule module;
    VkShaderStageFlagBits stage;
public:
    shader(context & ctx, VkShaderStageFlagBits stage, std::vector<uint32_t> words);
    ~shader();

    VkPipelineShaderStageCreateInfo get_shader_stage() const { return {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, stage, module, "main"}; }
};

class scene_pipeline
{
    std::shared_ptr<scene_pipeline_layout> layout;
    VkPipeline pipeline;
public:
    scene_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test);
    ~scene_pipeline();

    VkPipeline get_pipeline() const { return pipeline; }
    VkPipelineLayout get_pipeline_layout() const { return layout->get_pipeline_layout(); }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return layout->get_per_object_descriptor_set_layout(); }
};

struct gfx_mesh
{
    std::unique_ptr<static_buffer> vertex_buffer;
    std::unique_ptr<static_buffer> index_buffer;
    uint32_t index_count;
    mesh m;

    gfx_mesh(context & ctx, const mesh & m) :
        vertex_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.vertices.size() * sizeof(mesh::vertex), m.vertices.data())},
        index_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.triangles.size() * sizeof(uint3), m.triangles.data())},
        index_count{static_cast<uint32_t>(m.triangles.size() * 3)}, m{m}
    {
        
    }
};

struct draw_item 
{
    const scene_pipeline * pipeline;
    VkDescriptorSet set;
    const gfx_mesh * mesh;
    std::vector<size_t> mtls;
};
struct draw_list
{
    transient_resource_pool & pool;
    std::vector<draw_item> items;
    
    draw_list(transient_resource_pool & pool) : pool{pool} {}

    descriptor_set draw(const scene_pipeline & pipeline, const gfx_mesh & mesh, std::vector<size_t> mtls);
    descriptor_set draw(const scene_pipeline & pipeline, const gfx_mesh & mesh);
    void write_commands(VkCommandBuffer cmd) const;
};

class renderer
{
    context & ctx;
    shader_compiler compiler;
public:
    renderer(context & ctx) : ctx{ctx} {}

    std::shared_ptr<shader> create_shader(VkShaderStageFlagBits stage, const char * filename);
    std::shared_ptr<vertex_format> create_vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes);
    std::shared_ptr<scene_contract> create_contract(VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    std::shared_ptr<scene_pipeline_layout> create_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings);
    std::shared_ptr<scene_pipeline> create_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test);
};

#endif
