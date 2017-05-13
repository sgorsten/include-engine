#include "renderer.h"

vertex_format::vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes) :
    bindings{bindings.begin(), bindings.end()}, attributes{attributes.begin(), attributes.end()}
{

}

VkPipelineVertexInputStateCreateInfo vertex_format::get_vertex_input_state() const
{
    return {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()), bindings.data(), static_cast<uint32_t>(attributes.size()), attributes.data()};
}

scene_contract::scene_contract(context & ctx, array_view<VkRenderPass> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings) : 
    ctx{ctx}, render_passes{render_passes.begin(), render_passes.end()}, per_scene_layout{ctx.create_descriptor_set_layout(per_scene_bindings)}, per_view_layout{ctx.create_descriptor_set_layout(per_view_bindings)},
    example_layout{ctx.create_pipeline_layout({per_scene_layout, per_view_layout})} {}

scene_contract::~scene_contract()
{
    vkDestroyPipelineLayout(ctx.device, example_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_view_layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, per_scene_layout, nullptr);
}

scene_pipeline_layout::scene_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings) :
    contract{contract}, per_object_layout{contract->ctx.create_descriptor_set_layout(per_object_bindings)},
    pipeline_layout{contract->ctx.create_pipeline_layout({contract->per_scene_layout, contract->per_view_layout, per_object_layout})} {}

scene_pipeline_layout::~scene_pipeline_layout()
{
    vkDestroyPipelineLayout(contract->ctx.device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(contract->ctx.device, per_object_layout, nullptr);
}

shader::shader(context & ctx, VkShaderStageFlagBits stage, std::vector<uint32_t> words)
    : ctx{ctx}, module{ctx.create_shader_module(words)}, stage{stage}
{

}

shader::~shader()
{
    vkDestroyShaderModule(ctx.device, module, nullptr);
}

scene_pipeline::scene_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending) : 
    layout{layout}
{
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    for(auto & s : stages) shader_stages.push_back(s->get_shader_stage());
    for(auto & p : layout->get_contract().get_render_passes()) pipelines.push_back(make_pipeline(layout->get_device(), p, layout->get_pipeline_layout(), format->get_vertex_input_state(), shader_stages, depth_write, depth_test, additive_blending));
}
    
scene_pipeline::~scene_pipeline()
{
    for(auto & p : pipelines) vkDestroyPipeline(layout->get_device(), p, nullptr);
}

void scene_descriptor_set::write_uniform_buffer(uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info)
{
    vkWriteDescriptorBufferInfo(layout->get_device(), set, binding, array_element, info);
}

void scene_descriptor_set::write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout)
{
    vkWriteDescriptorCombinedImageSamplerInfo(layout->get_device(), set, binding, array_element, {sampler, image_view, image_layout});
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls, VkDescriptorBufferInfo instances, size_t instance_stride)
{
    if(&pipeline.get_contract() != &contract) fail_fast();
    if(&pipeline.get_layout() != &descriptors.get_pipeline_layout()) fail_fast();

    draw_item item {&pipeline, descriptors.get_descriptor_set()};
    item.vertex_buffer_count = instance_stride ? 2 : 1;
    item.vertex_buffers[0] = *mesh.vertex_buffer;
    item.vertex_buffers[1] = instances.buffer;
    item.vertex_buffer_offsets[0] = 0;
    item.vertex_buffer_offsets[1] = instances.offset;
    item.index_buffer = *mesh.index_buffer;
    item.index_buffer_offset = 0;
    item.instance_count = instance_stride ? instances.range / instance_stride : 1;
    for(auto mtl : mtls)
    {
        item.first_index = mesh.m.materials[mtl].first_triangle*3;
        item.index_count = mesh.m.materials[mtl].num_triangles*3;
        items.push_back(item);
    }
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, VkDescriptorBufferInfo instances, size_t instance_stride)
{
    std::vector<size_t> mtls;
    for(size_t i=0; i<mesh.m.materials.size(); ++i) mtls.push_back(i);
    draw(pipeline, descriptors, mesh, mtls, instances, instance_stride);
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls)
{
    draw(pipeline, descriptors, mesh, mtls, {}, 0);
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh)
{
    draw(pipeline, descriptors, mesh, {}, 0);
}

void draw_list::write_commands(VkCommandBuffer cmd, VkRenderPass render_pass) const
{
    auto render_pass_index = contract.get_render_pass_index(render_pass);
    for(auto & item : items)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline(render_pass_index));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline_layout(), 2, {item.set}, {});
        vkCmdBindVertexBuffers(cmd, 0, item.vertex_buffer_count, item.vertex_buffers, item.vertex_buffer_offsets);
        vkCmdBindIndexBuffer(cmd, item.index_buffer, item.index_buffer_offset, VkIndexType::VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, item.index_count, item.instance_count, item.first_index, 0, 0);
    }
}

std::shared_ptr<shader> renderer::create_shader(VkShaderStageFlagBits stage, const char * filename)
{
    return std::make_shared<shader>(ctx, stage, compiler.compile_glsl(stage, filename));
}

std::shared_ptr<vertex_format> renderer::create_vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes)
{
    return std::make_shared<vertex_format>(bindings, attributes);
}

std::shared_ptr<scene_contract> renderer::create_contract(array_view<VkRenderPass> render_passes, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings)
{
    return std::make_shared<scene_contract>(ctx, render_passes, per_scene_bindings, per_view_bindings);
}

std::shared_ptr<scene_pipeline_layout> renderer::create_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings)
{
    return std::make_shared<scene_pipeline_layout>(contract, per_object_bindings);
}

std::shared_ptr<scene_pipeline> renderer::create_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test, bool additive_blending)
{
    return std::make_shared<scene_pipeline>(layout, format, stages, depth_write, depth_test, additive_blending);
}