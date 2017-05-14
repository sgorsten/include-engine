#ifndef RENDERER_H
#define RENDERER_H

#include "data-types.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

[[noreturn]] void fail_fast();
const char * to_string(VkResult result);
void check(VkResult result);

struct context;

class window
{
    std::shared_ptr<context> ctx;

    GLFWwindow * glfw_window {};
    VkSurfaceKHR surface {};
    VkSwapchainKHR swapchain {};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    VkSemaphore image_available {}, render_finished {};
    uint2 dims;
public:
    window(std::shared_ptr<context> ctx, uint2 dims, const char * title);
    ~window();

    const std::vector<VkImage> & get_swapchain_images() const { return swapchain_images; }
    const std::vector<VkImageView> & get_swapchain_image_views() const { return swapchain_image_views; }
    uint2 get_dims() const { return dims; }
    float get_aspect() const { return (float)dims.x/dims.y; }
    bool should_close() const { return !!glfwWindowShouldClose(glfw_window); }

    float2 get_cursor_pos() const { double2 c; glfwGetCursorPos(glfw_window, &c.x, &c.y); return float2{c}; }
    bool get_mouse_button(int button) const { return glfwGetMouseButton(glfw_window, button) == GLFW_PRESS; }
    bool get_key(int key) const { return glfwGetKey(glfw_window, key) == GLFW_PRESS; }

    uint32_t begin();
    void end(uint32_t index, array_view<VkCommandBuffer> commands, VkFence fence);
};

class render_target
{
    std::shared_ptr<context> ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    render_target(std::shared_ptr<context> ctx, uint2 dims, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    ~render_target();

    VkImage get_image() const { return image; }
    VkImageView get_image_view() const { return image_view; }
};

inline render_target make_depth_buffer(std::shared_ptr<context> ctx, uint2 dims) { return {ctx, dims, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT}; }

class texture_2d
{
    std::shared_ptr<context> ctx;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    texture_2d(std::shared_ptr<context> ctx, uint32_t width, uint32_t height, VkFormat format, const void * initial_data);
    texture_2d(std::shared_ptr<context> ctx, VkFormat format, const ::image & image) : texture_2d(ctx, image.get_width(), image.get_height(), format, image.get_pixels()) {}
    ~texture_2d();

    VkImage get_image() { return image; }
    operator VkImageView () const { return image_view; }
};

class texture_cube
{
    std::shared_ptr<context> ctx;
    VkImage img;
    VkImageView image_view;
    VkDeviceMemory device_memory;
public:
    texture_cube(std::shared_ptr<context> ctx, VkFormat format, const image & posx, const image & negx, const image & posy, const image & negy, const image & posz, const image & negz);
    ~texture_cube();

    VkImage get_image() { return img; }
    operator VkImageView () const { return image_view; }
};

class static_buffer
{
    std::shared_ptr<context> ctx;
    VkBuffer buffer;
    VkDeviceMemory device_memory;
public:
    static_buffer(std::shared_ptr<context> ctx, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties, VkDeviceSize size, const void * initial_data);
    ~static_buffer();

    operator VkBuffer () { return buffer; }
};

class dynamic_buffer
{
    std::shared_ptr<context> ctx;
    VkBuffer buffer {};
    VkMemoryRequirements mem_reqs {};
    VkDeviceMemory device_memory {};
    char * mapped_memory {};
    VkDeviceSize offset {}, range {};
public:
    dynamic_buffer(std::shared_ptr<context> ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties);
    ~dynamic_buffer();

    void reset();
    
    void begin();
    void write(size_t size, const void * data);
    VkDescriptorBufferInfo end();

    VkDescriptorBufferInfo upload(size_t size, const void * data);
};

// Manages the allocation of short-lived resources which can be recycled in a single call, protected by a fence
class transient_resource_pool
{
    std::shared_ptr<context> ctx;
    dynamic_buffer uniform_buffer;
    dynamic_buffer vertex_buffer;
    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;
    VkDescriptorPool descriptor_pool;
    std::vector<VkDescriptorSet> descriptor_sets;
    VkFence fence;
public:
    transient_resource_pool(std::shared_ptr<context> ctx, array_view<VkDescriptorPoolSize> descriptor_pool_sizes, uint32_t max_descriptor_sets);
    ~transient_resource_pool();

    void reset();
    VkCommandBuffer allocate_command_buffer();
    VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout layout);
    VkDescriptorBufferInfo write_data(size_t size, const void * data) { return uniform_buffer.upload(size, data); }

    void begin_vertices() { vertex_buffer.begin(); }
    template<class T> void write_vertex(const T & vertex) { vertex_buffer.write(sizeof(vertex), &vertex); }
    VkDescriptorBufferInfo end_vertices() { return vertex_buffer.end(); }

    void begin_instances() { begin_vertices(); }
    template<class T> void write_instance(const T & instance) { write_vertex(instance); }
    VkDescriptorBufferInfo end_instances() { return end_vertices(); }

    VkFence get_fence() { return fence; }

    template<class T> VkDescriptorBufferInfo write_data(const T & data) { return write_data(sizeof(data), &data); }
};

// Other utility functions
void transition_layout(VkCommandBuffer command_buffer, VkImage image, uint32_t mip_level, uint32_t array_layer, VkImageLayout old_layout, VkImageLayout new_layout);

// Convenience wrappers around Vulkan calls
void vkUpdateDescriptorSets(VkDevice device, array_view<VkWriteDescriptorSet> descriptorWrites, array_view<VkCopyDescriptorSet> descriptorCopies);
void vkWriteDescriptorCombinedImageSamplerInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorImageInfo info);
void vkWriteDescriptorBufferInfo(VkDevice device, VkDescriptorSet set, uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info);

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, array_view<VkDescriptorSet> descriptorSets, array_view<uint32_t> dynamicOffsets);
void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, array_view<VkBuffer> buffers, array_view<VkDeviceSize> offsets);

void vkCmdSetViewport(VkCommandBuffer commandBuffer, VkRect2D viewport);
void vkCmdSetScissor(VkCommandBuffer commandBuffer, VkRect2D scissor);
void vkCmdBeginRenderPass(VkCommandBuffer cmd, VkRenderPass renderPass, VkFramebuffer framebuffer, VkRect2D renderArea, array_view<VkClearValue> clearValues);

#include "load.h"   // For shader_compiler
#include <map>

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

    gfx_mesh(std::shared_ptr<context> ctx, const mesh & m) :
        vertex_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.vertices.size() * sizeof(mesh::vertex), m.vertices.data())},
        index_buffer{std::make_unique<static_buffer>(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m.triangles.size() * sizeof(uint3), m.triangles.data())},
        index_count{static_cast<uint32_t>(m.triangles.size() * 3)}, m{m}
    {
        
    }
};

class shader
{
    std::shared_ptr<context> ctx;
    VkShaderModule module;
    VkShaderStageFlagBits stage;
public:
    shader(std::shared_ptr<context> ctx, VkShaderStageFlagBits stage, array_view<uint32_t> words);
    ~shader();

    VkPipelineShaderStageCreateInfo get_shader_stage() const { return {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, stage, module, "main"}; }
};

class sampler
{
    std::shared_ptr<context> ctx;
    VkSampler handle;
public:
    sampler(std::shared_ptr<context> ctx, const VkSamplerCreateInfo & create_info);
    ~sampler();

    VkSampler get_vk_handle() const { return handle; }
};

class render_pass
{
    std::shared_ptr<context> ctx;
    VkRenderPass handle;
public:
    render_pass(std::shared_ptr<context> ctx, array_view<VkAttachmentDescription> color_attachments, std::optional<VkAttachmentDescription> depth_attachment);
    ~render_pass();

    VkRenderPass get_vk_handle() const { return handle; }
};

class framebuffer
{
    std::shared_ptr<context> ctx;
    std::shared_ptr<const render_pass> pass;
    VkFramebuffer handle;
    uint2 dims;
public:
    framebuffer(std::shared_ptr<context> ctx, std::shared_ptr<const render_pass> pass, array_view<VkImageView> attachments, uint2 dims);
    ~framebuffer();

    const render_pass & get_render_pass() const { return *pass; }
    VkFramebuffer get_vk_handle() const { return handle; }
    uint2 get_dimensions() const { return dims; }
};    

// A scene contract defines the common functionality of a group of items which will be drawn together. It
// defines the set of possible render passes these items could be drawn in (such as to a color buffer or
// to a shadow buffer), as well as the descriptor sets that will be used in common by all shaders executing
// under this contract.
class scene_contract
{
    friend class scene_material;
    std::shared_ptr<context> ctx;
    std::vector<std::shared_ptr<const render_pass>> render_passes;    // List of possible render passes that pipelines obeying this contract can participate in
    VkDescriptorSetLayout per_scene_layout;     // Descriptors which are shared by the entire scene
    VkDescriptorSetLayout per_view_layout;      // Descriptors which vary per unique view into the scene
    VkPipelineLayout example_layout;            // Layout for a pipeline which has no per-object descriptor set
public:
    scene_contract(std::shared_ptr<context> ctx, array_view<std::shared_ptr<const render_pass>> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    ~scene_contract();

    size_t get_render_pass_index(const render_pass & pass) const { for(size_t i=0; i<render_passes.size(); ++i) if(render_passes[i].get() == &pass) return i; throw std::logic_error("render pass not part of contract"); }
    VkDescriptorSetLayout get_per_scene_layout() const { return per_scene_layout; }
    VkDescriptorSetLayout get_per_view_layout() const { return per_view_layout; }
    VkPipelineLayout get_example_layout() const { return example_layout; }
};

// A scene material consists of a set of shader stages known to conform to a specific scene contract.
// For each material, several pipelines will be precomputed, based on the render passes described in
// the contract, but these pipelines will all share a common layout, allowing them to be used in an
// interchangeable fashion.
class scene_material
{
    friend class scene_descriptor_set;
    std::shared_ptr<context> ctx;
    std::shared_ptr<scene_contract> contract;
    VkDescriptorSetLayout per_object_layout;
    VkPipelineLayout pipeline_layout;
    std::vector<VkPipeline> pipelines;
public:
    scene_material(std::shared_ptr<context> ctx, std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending);
    ~scene_material();

    const scene_contract & get_contract() const { return *contract; }
    VkDescriptorSetLayout get_per_object_descriptor_set_layout() const { return per_object_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }
    VkPipeline get_pipeline(size_t render_pass_index) const { return pipelines[render_pass_index]; }    
};

/////////

class renderer
{
public:
    std::shared_ptr<context> ctx;
private:
    shader_compiler compiler;
public:
    renderer();

    VkDevice get_device();
    VkFormat get_swapchain_surface_format() const;

    std::shared_ptr<render_pass> create_render_pass(array_view<VkAttachmentDescription> color_attachments, std::optional<VkAttachmentDescription> depth_attachment);
    std::shared_ptr<framebuffer> create_framebuffer(std::shared_ptr<const render_pass> render_pass, array_view<VkImageView> attachments, uint2 dims);

    std::shared_ptr<shader> create_shader(VkShaderStageFlagBits stage, const char * filename);
    std::shared_ptr<vertex_format> create_vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes);
    std::shared_ptr<scene_contract> create_contract(array_view<std::shared_ptr<const render_pass>> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings);
    std::shared_ptr<scene_material> create_material(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// A draw list records draw calls, which can be sorted and written into multiple command buffers. //
////////////////////////////////////////////////////////////////////////////////////////////////////

class scene_descriptor_set
{
    const scene_material * material;
    VkDescriptorSet set;
public:
    scene_descriptor_set(transient_resource_pool & pool, const scene_material & material) : material{&material}, set{pool.allocate_descriptor_set(material.get_per_object_descriptor_set_layout())} {}

    const scene_material & get_material() const { return *material; }
    VkDescriptorSet get_descriptor_set() const { return set; }

    void write_uniform_buffer(uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info);
    void write_combined_image_sampler(uint32_t binding, uint32_t array_element, sampler & sampler, VkImageView image_view, VkImageLayout image_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};

struct draw_item 
{
    const scene_material * material;
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

    scene_descriptor_set descriptor_set(const scene_material & material) { return {pool, material}; }    

    void draw(const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls, VkDescriptorBufferInfo instances, size_t instance_stride);
    void draw(const scene_descriptor_set & descriptors, const gfx_mesh & mesh, VkDescriptorBufferInfo instances, size_t instance_stride);
    void draw(const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls);
    void draw(const scene_descriptor_set & descriptors, const gfx_mesh & mesh);
    void write_commands(VkCommandBuffer cmd, const render_pass & render_pass) const;
};

#endif
