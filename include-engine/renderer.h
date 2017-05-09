#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan.h" // For Vulkan API access
#include "load.h"   // For shader_compiler

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

struct gfx_mesh
{
    std::unique_ptr<static_buffer> vertex_buffer;
    std::unique_ptr<static_buffer> index_buffer;
    uint32_t index_count;
    mesh m;

    gfx_mesh(std::unique_ptr<static_buffer> vertex_buffer, std::unique_ptr<static_buffer> index_buffer, uint32_t index_count)
        : vertex_buffer{move(vertex_buffer)}, index_buffer{move(index_buffer)}, index_count{index_count}
    {
        m.materials.push_back({"", 0, index_count/3});
    }

    gfx_mesh(context & ctx, const mesh & m) :
        vertex_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.vertices.size() * sizeof(mesh::vertex), m.vertices.data())},
        index_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.triangles.size() * sizeof(uint3), m.triangles.data())},
        index_count{static_cast<uint32_t>(m.triangles.size() * 3)}, m{m}
    {
        
    }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// A scene contract defines the common behavior of a bunch of draw calls which belong to the same scene. //
// They must occur within a single render pass, and must use the same layouts for per-scene and per-view //
// descriptor sets.                                                                                      //
///////////////////////////////////////////////////////////////////////////////////////////////////////////

class scene_contract
{
    friend class scene_pipeline_layout;
    context & ctx;
    VkRenderPass render_pass;
    VkDescriptorSetLayout per_scene_layout; // Descriptors which are shared by the entire scene
    VkDescriptorSetLayout per_view_layout;  // Descriptors which vary per unique view into the scene
    VkPipelineLayout example_layout;        // Layout for a pipeline which has no per-object descriptor set
public:
    scene_contract(context & ctx, VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    ~scene_contract();

    VkDescriptorSetLayout get_per_scene_layout() const { return per_scene_layout; }
    VkDescriptorSetLayout get_per_view_layout() const { return per_view_layout; }
    VkPipelineLayout get_example_layout() const { return example_layout; }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////
// A scene pipeline layout defines the common behavior of pipelines which conform to the same scene //
// contract, and additionally use the same layout for per-object descriptor sets.                   //
//////////////////////////////////////////////////////////////////////////////////////////////////////

class scene_pipeline_layout
{
    std::shared_ptr<scene_contract> contract;
    VkDescriptorSetLayout per_object_layout;
    VkPipelineLayout pipeline_layout;
public:
    scene_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings);
    ~scene_pipeline_layout();

    const scene_contract & get_contract() const { return *contract; }
    VkDevice get_device() const { return contract->ctx.device; }
    VkRenderPass get_render_pass() const { return contract->render_pass; }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return per_object_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }
};

////////////////////////////////////////////////////////////////////////////////////////////////
// A scene pipeline defines a pipeline that is known to conform to a specific scene contract. //
////////////////////////////////////////////////////////////////////////////////////////////////

class scene_pipeline
{
    std::shared_ptr<scene_pipeline_layout> layout;
    VkPipeline pipeline;
public:
    scene_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending);
    ~scene_pipeline();

    const scene_contract & get_contract() const { return layout->get_contract(); }
    const scene_pipeline_layout & get_layout() const { return *layout; }
    VkPipeline get_pipeline() const { return pipeline; }
    VkPipelineLayout get_pipeline_layout() const { return layout->get_pipeline_layout(); }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return layout->get_per_object_descriptor_set_layout(); }
};

/////////

class scene_descriptor_set
{
    const scene_pipeline_layout * layout;
    VkDescriptorSet set;
public:
    scene_descriptor_set(transient_resource_pool & pool, const scene_pipeline_layout & layout) : layout{&layout}, set{pool.allocate_descriptor_set(layout.get_per_object_descriptor_set_layout())} {}
    scene_descriptor_set(transient_resource_pool & pool, const scene_pipeline & pipeline) : scene_descriptor_set{pool, pipeline.get_layout()} {}

    const scene_pipeline_layout & get_pipeline_layout() const { return *layout; }
    VkDescriptorSet get_descriptor_set() const { return set; }

    void write_uniform_buffer(uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info);
    void write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// A draw list records draw calls, which can be sorted and written into multiple command buffers. //
////////////////////////////////////////////////////////////////////////////////////////////////////

struct draw_item 
{
    const scene_pipeline * pipeline;
    VkDescriptorSet set;
    uint32_t vertex_buffer_count;
    VkBuffer vertex_buffers[4];
    VkDeviceSize vertex_buffer_offsets[4];
    VkBuffer index_buffer;
    VkDeviceSize index_buffer_offset;
    size_t first_index, index_count;
    size_t instance_count;
};
struct draw_list
{
    transient_resource_pool & pool;
    const scene_contract & contract;
    std::vector<draw_item> items;
    
    draw_list(transient_resource_pool & pool, const scene_contract & contract) : pool{pool}, contract{contract} {}

    template<class T> VkDescriptorBufferInfo upload_uniforms(const T & uniforms) { return pool.write_data(uniforms); }

    void begin_instances() { pool.begin_instances(); }
    template<class T> void write_instance(const T & instance) { pool.write_instance(instance); }
    VkDescriptorBufferInfo end_instances() { return pool.end_instances(); }

    scene_descriptor_set descriptor_set(const scene_pipeline & pipeline) { return {pool, pipeline}; }    

    void draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls, VkDescriptorBufferInfo instances, size_t instance_stride);
    void draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, VkDescriptorBufferInfo instances, size_t instance_stride);
    void draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls);
    void draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh);
    void write_commands(VkCommandBuffer cmd) const;
};

class renderer
{
public:
    context & ctx;
private:
    shader_compiler compiler;
public:
    renderer(context & ctx) : ctx{ctx} {}

    std::shared_ptr<shader> create_shader(VkShaderStageFlagBits stage, const char * filename);
    std::shared_ptr<vertex_format> create_vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes);
    std::shared_ptr<scene_contract> create_contract(VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    std::shared_ptr<scene_pipeline_layout> create_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings);
    std::shared_ptr<scene_pipeline> create_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending);
};

#endif
