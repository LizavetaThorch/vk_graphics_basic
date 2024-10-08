#include "shadowmap_render.h"

#include <geom/vk_mesh.h>
#include <vk_pipeline.h>
#include <vk_buffers.h>
#include <iostream>
#include <random>

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan_core.h>


/// RESOURCE ALLOCATION

void SimpleShadowmapRender::AllocateResources()
{
  mainViewDepth        = m_context->createImage(etna::Image::CreateInfo
  {
           .extent     = vk::Extent3D{ m_width, m_height, 1 },
           .name       = "main_view_depth",
           .format     = vk::Format::eD32Sfloat,
           .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment 
  });

  shadowMap            = m_context->createImage(etna::Image::CreateInfo
  {
           .extent     = vk::Extent3D{ 2048, 2048, 1 },
           .name       = "shadow_map",
           .format     = vk::Format::eD16Unorm,
           .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled 
  });
  
  normal      = m_context->createImage(etna::Image::CreateInfo
  {
           .extent     = vk::Extent3D{ 2048, 2048, 1 },
           .name       = "shadow_map_normal",
           .format     = vk::Format::eR32G32B32A32Sfloat,
           .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled 
  });
  
  flux        = m_context->createImage(etna::Image::CreateInfo
  {
           .extent     = vk::Extent3D{ 2048, 2048, 1 },
           .name       = "shadow_map_flux",
           .format     = vk::Format::eR32G32B32A32Sfloat,
           .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled 
  });

  samplePositions      = m_context->createBuffer(etna::Buffer::CreateInfo
  {
          .size        = sizeof(float2) * 800,
          .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
          .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
          .name        = "sample_positions" 
  });

  float2 *samplePositionsData = reinterpret_cast<float2 *>(samplePositions.map());

  std::mt19937 random;

  std::uniform_real_distribution<float> genDistance(0.0, 1.0);
  std::uniform_real_distribution<float> genAngle(0.0, 2.0 * M_PI);

  for (size_t i = 0; i < 800; ++i)
  {
    float distance = genDistance(random);
    float angle    = genAngle(random);

    samplePositionsData[i] = distance * float2(std::cos(angle), std::sin(angle));
  }

  samplePositions.unmap();

  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{ .name = "default" });
  constants      = m_context->createBuffer(etna::Buffer::CreateInfo{
         .size        = sizeof(UniformParams),
         .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
         .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
         .name        = "constants" });

  m_uboMappedMem = constants.map();
}

void SimpleShadowmapRender::LoadScene(const char *path, bool transpose_inst_matrices)
{
  m_pScnMgr->LoadSceneXML(path, transpose_inst_matrices);

  // TODO: Make a separate stage
  loadShaders();
  PreparePipelines();

  auto loadedCam = m_pScnMgr->GetCamera(0);
  m_cam.fov      = loadedCam.fov;
  m_cam.pos      = float3(loadedCam.pos);
  m_cam.up       = float3(loadedCam.up);
  m_cam.lookAt   = float3(loadedCam.lookAt);
  m_cam.tdist    = loadedCam.farPlane;
}

void SimpleShadowmapRender::DeallocateResources()
{
  mainViewDepth.reset();// TODO: Make an etna method to reset all the resources
  shadowMap.reset();
  m_swapchain.Cleanup();
  vkDestroySurfaceKHR(GetVkInstance(), m_surface, nullptr);

  constants = etna::Buffer();
}


/// PIPELINES CREATION

void SimpleShadowmapRender::PreparePipelines()
{
  // create full screen quad for debug purposes
  //
  m_pQuad = std::make_unique<QuadRenderer>(QuadRenderer::CreateInfo{
    .format = static_cast<vk::Format>(m_swapchain.GetFormat()),
    .rect   = { 0, 0, 512, 512 },
  });
  SetupSimplePipeline();
}

void SimpleShadowmapRender::loadShaders()
{
  etna::create_program("simple_shadow",
    { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple_shadow.frag.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
  etna::create_program("rsm_lighting_shade",
    { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/rsm_lighting_shade.frag.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
  etna::create_program("normal_flux",
    { VK_GRAPHICS_BASIC_ROOT "/resources/shaders/normal_flux.frag.spv", VK_GRAPHICS_BASIC_ROOT "/resources/shaders/simple.vert.spv" });
}

void SimpleShadowmapRender::SetupSimplePipeline()
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = { etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = m_pScnMgr->GetVertexStreamDescription() } }
  };

  auto &pipelineManager      = etna::get_context().getPipelineManager();
  m_basicForwardPipeline     = pipelineManager.createGraphicsPipeline("simple_shadow",
        { .vertexShaderInput    = sceneVertexInputDesc,
          .fragmentShaderOutput = {
            .colorAttachmentFormats = { static_cast<vk::Format>(m_swapchain.GetFormat()) },
            .depthAttachmentFormat  = vk::Format::eD32Sfloat } });
  m_lightingShadePipeline = pipelineManager.createGraphicsPipeline("rsm_lighting_shade",
    { .vertexShaderInput    = sceneVertexInputDesc,
      .fragmentShaderOutput = {
        .colorAttachmentFormats = { static_cast<vk::Format>(m_swapchain.GetFormat()) },
        .depthAttachmentFormat  = vk::Format::eD32Sfloat } });
  m_shadowPipeline   = pipelineManager.createGraphicsPipeline("normal_flux",
              { .vertexShaderInput = sceneVertexInputDesc,
                .blendingConfig    = {
                     .attachments  = {
                          vk::PipelineColorBlendAttachmentState{
                                         .blendEnable    = vk::False,
                                         .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                          },
                          vk::PipelineColorBlendAttachmentState{
                                         .blendEnable    = vk::False,
                                         .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                          },
                     } },
                .fragmentShaderOutput    = { .colorAttachmentFormats = { vk::Format::eR32G32B32A32Sfloat, vk::Format::eR32G32B32A32Sfloat }, .depthAttachmentFormat = vk::Format::eD16Unorm } });
}


/// COMMAND BUFFER FILLING

void SimpleShadowmapRender::DrawSceneCmd(VkCommandBuffer a_cmdBuff, const float4x4 &a_wvp, VkPipelineLayout a_pipelineLayout)
{
  VkShaderStageFlags stageFlags = (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

  VkDeviceSize zero_offset = 0u;
  VkBuffer vertexBuf       = m_pScnMgr->GetVertexBuffer();
  VkBuffer indexBuf        = m_pScnMgr->GetIndexBuffer();

  vkCmdBindVertexBuffers(a_cmdBuff, 0, 1, &vertexBuf, &zero_offset);
  vkCmdBindIndexBuffer(a_cmdBuff, indexBuf, 0, VK_INDEX_TYPE_UINT32);

  pushConst2M.projView = a_wvp;
  for (uint32_t i = 0; i < m_pScnMgr->InstancesNum(); ++i)
  {
    auto inst         = m_pScnMgr->GetInstanceInfo(i);
    pushConst2M.model = m_pScnMgr->GetInstanceMatrix(i);

    float hue = i / static_cast<float>(m_pScnMgr->InstancesNum());
    switch (static_cast<int>(hue * 6) % 6)
    {
    case 0:
      pushConst2M.objectColor = float4(1.0, hue * 6, 0.0, 1.0);
      break;
    case 1:
      pushConst2M.objectColor = float4(2.0 - hue * 6.0, 1.0, 0.0, 1.0);
      break;
    case 2:
      pushConst2M.objectColor = float4(0.0, 1.0, hue * 6.0 - 2.0, 1.0);
      break;
    case 3:
      pushConst2M.objectColor = float4(0.0, 4.0 - hue * 6.0, 1.0, 1.0);
      break;
    case 4:
      pushConst2M.objectColor = float4(hue * 6.0 - 4.0, 0.0, 1.0, 1.0);
      break;
    case 5:
      pushConst2M.objectColor = float4(1.0, 0.0, 6.0 - hue * 6.0, 1.0);
      break;
    }

    vkCmdPushConstants(a_cmdBuff, a_pipelineLayout, stageFlags, 0, sizeof(pushConst2M), &pushConst2M);

    auto mesh_info = m_pScnMgr->GetMeshInfo(inst.mesh_id);
    vkCmdDrawIndexed(a_cmdBuff, mesh_info.m_indNum, 1, mesh_info.m_indexOffset, mesh_info.m_vertexOffset, 0);
  }
}

void SimpleShadowmapRender::BuildCommandBufferSimple(VkCommandBuffer a_cmdBuff, VkImage a_targetImage, VkImageView a_targetImageView)
{
  vkResetCommandBuffer(a_cmdBuff, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(a_cmdBuff, &beginInfo));

  //// draw scene to shadowmap
  //
  {
    etna::RenderTargetState renderTargets(a_cmdBuff, 
        { 0, 0, 2048, 2048 }, 
        { { normal.get(), normal.getView({}) }, 
        { flux.get(), flux.getView({}) } }, 
        { shadowMap.get(), shadowMap.getView({}) });

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline.getVkPipeline());
    DrawSceneCmd(a_cmdBuff, m_lightMatrix, m_shadowPipeline.getVkPipelineLayout());
  }

  //// draw final scene to screen
  //
  if (m_rsm)
  {
    auto simpleMaterialInfo = etna::get_shader_program("rsm_lighting_shade");

    auto set = etna::create_descriptor_set(simpleMaterialInfo.getDescriptorLayoutId(0), a_cmdBuff, 
        { 
            etna::Binding{ 0, constants.genBinding() }, 
            etna::Binding{ 1, shadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }, 
            etna::Binding{ 2, normal.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }, 
            etna::Binding{ 3, flux.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) }, 
            etna::Binding{ 4, samplePositions.genBinding() } 
        });

    VkDescriptorSet vkSet = set.getVkSet();

    etna::RenderTargetState renderTargets(a_cmdBuff, { 0, 0, m_width, m_height }, { {a_targetImage, a_targetImageView } }, {mainViewDepth.get(), mainViewDepth.getView({}) });

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingShadePipeline.getVkPipeline());
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingShadePipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);

    DrawSceneCmd(a_cmdBuff, m_worldViewProj, m_lightingShadePipeline.getVkPipelineLayout());
  }
  else
  {
    auto simpleMaterialInfo = etna::get_shader_program("simple_shadow");

    auto set = etna::create_descriptor_set(simpleMaterialInfo.getDescriptorLayoutId(0), a_cmdBuff, 
        { 
            etna::Binding{ 0, constants.genBinding() }, 
            etna::Binding{ 1, shadowMap.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal) } 
        });

    VkDescriptorSet vkSet = set.getVkSet();

    etna::RenderTargetState renderTargets(a_cmdBuff, { 0, 0, m_width, m_height }, { { a_targetImage, a_targetImageView } }, { mainViewDepth.get(), mainViewDepth.getView({}) });

    vkCmdBindPipeline(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.getVkPipeline());
    vkCmdBindDescriptorSets(a_cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, m_basicForwardPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, VK_NULL_HANDLE);

    DrawSceneCmd(a_cmdBuff, m_worldViewProj, m_basicForwardPipeline.getVkPipelineLayout());
  }

  if (m_input.drawFSQuad)
    m_pQuad->RecordCommands(a_cmdBuff, a_targetImage, a_targetImageView, flux, defaultSampler);

  etna::set_state(a_cmdBuff, a_targetImage, 
      vk::PipelineStageFlagBits2::eBottomOfPipe, 
      vk::AccessFlags2(), 
      vk::ImageLayout::ePresentSrcKHR, 
      vk::ImageAspectFlagBits::eColor);

  etna::finish_frame(a_cmdBuff);

  VK_CHECK_RESULT(vkEndCommandBuffer(a_cmdBuff));
}
