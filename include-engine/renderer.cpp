#include "renderer.h"

vertex_format::vertex_format(array_view<VkVertexInputBindingDescription> bindings, array_view<VkVertexInputAttributeDescription> attributes) :
    bindings{bindings.begin(), bindings.end()}, attributes{attributes.begin(), attributes.end()}
{

}

VkPipelineVertexInputStateCreateInfo vertex_format::get_vertex_input_state() const
{
    return {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()), bindings.data(), static_cast<uint32_t>(attributes.size()), attributes.data()};
}

scene_contract::scene_contract(context & ctx, VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings) : 
    ctx{ctx}, render_pass{render_pass}, per_scene_layout{ctx.create_descriptor_set_layout(per_scene_bindings)}, per_view_layout{ctx.create_descriptor_set_layout(per_view_bindings)},
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

scene_pipeline::scene_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test) : 
    layout{layout}
{
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    for(auto & s : stages) shader_stages.push_back(s->get_shader_stage());
    pipeline = make_pipeline(layout->get_device(), layout->get_render_pass(), layout->get_pipeline_layout(), format->get_vertex_input_state(), shader_stages, depth_write, depth_test); 
}
    
scene_pipeline::~scene_pipeline()
{
    vkDestroyPipeline(layout->get_device(), pipeline, nullptr);
}

void scene_descriptor_set::write_uniform_buffer(uint32_t binding, uint32_t array_element, VkDescriptorBufferInfo info)
{
    vkWriteDescriptorBufferInfo(layout->get_device(), set, binding, array_element, info);
}

void scene_descriptor_set::write_combined_image_sampler(uint32_t binding, uint32_t array_element, VkSampler sampler, VkImageView image_view, VkImageLayout image_layout)
{
    vkWriteDescriptorCombinedImageSamplerInfo(layout->get_device(), set, binding, array_element, {sampler, image_view, image_layout});
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh, std::vector<size_t> mtls)
{
    if(&pipeline.get_contract() != &contract) fail_fast();
    if(&pipeline.get_layout() != &descriptors.get_pipeline_layout()) fail_fast();
    items.push_back({&pipeline, descriptors.get_descriptor_set(), &mesh, mtls});
}

void draw_list::draw(const scene_pipeline & pipeline, const scene_descriptor_set & descriptors, const gfx_mesh & mesh)
{
    std::vector<size_t> mtls;
    for(size_t i=0; i<mesh.m.materials.size(); ++i) mtls.push_back(i);
    return draw(pipeline, descriptors, mesh, mtls);
}

void draw_list::write_commands(VkCommandBuffer cmd) const
{
    for(auto & item : items)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, item.pipeline->get_pipeline_layout(), 2, {item.set}, {});
        vkCmdBindVertexBuffers(cmd, 0, {*item.mesh->vertex_buffer}, {0});
        vkCmdBindIndexBuffer(cmd, *item.mesh->index_buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
        for(auto mtl : item.mtls) vkCmdDrawIndexed(cmd, item.mesh->m.materials[mtl].num_triangles*3, 1, item.mesh->m.materials[mtl].first_triangle*3, 0, 0);
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

std::shared_ptr<scene_contract> renderer::create_contract(VkRenderPass render_pass, array_view<VkDescriptorSetLayoutBinding> per_scene_bindings, array_view<VkDescriptorSetLayoutBinding> per_view_bindings)
{
    return std::make_shared<scene_contract>(ctx, render_pass, per_scene_bindings, per_view_bindings);
}

std::shared_ptr<scene_pipeline_layout> renderer::create_pipeline_layout(std::shared_ptr<scene_contract> contract, array_view<VkDescriptorSetLayoutBinding> per_object_bindings)
{
    return std::make_shared<scene_pipeline_layout>(contract, per_object_bindings);
}

std::shared_ptr<scene_pipeline> renderer::create_pipeline(std::shared_ptr<scene_pipeline_layout> layout, std::shared_ptr<vertex_format> format, array_view<std::shared_ptr<shader>> stages, bool depth_write, bool depth_test)
{
    return std::make_shared<scene_pipeline>(layout, format, stages, depth_write, depth_test);
}