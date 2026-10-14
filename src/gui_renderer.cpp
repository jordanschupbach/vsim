#include "gui_renderer.hpp"
#include "gui_shaders.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <vector>

namespace vsim {
namespace {
template <typename T> T required(T value, const char* name) {
    if (!value)
        throw std::runtime_error(std::string("cannot create GUI ") + name);
    return value;
}
struct Buffers {
    nvrhi::BufferHandle vertices, indices;
};
} // namespace
struct GuiRenderer::Impl {
    nvrhi::IDevice& device;
    nvrhi::TextureHandle font;
    nvrhi::SamplerHandle sampler;
    nvrhi::ShaderHandle vertex_shader, pixel_shader;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::BindingSetHandle bindings;
    nvrhi::GraphicsPipelineHandle pipeline;
    std::map<std::size_t, Buffers> slots;

    explicit Impl(nvrhi::IDevice& gpu) : device(gpu) {}

    void initialize(ImFontAtlas& fonts) {
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        fonts.GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0)
            throw std::runtime_error("GUI font atlas is empty");
        font = required(device.createTexture(
                            nvrhi::TextureDesc()
                                .setWidth(static_cast<std::uint32_t>(width))
                                .setHeight(static_cast<std::uint32_t>(height))
                                .setFormat(nvrhi::Format::RGBA8_UNORM)
                                .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
                                .setDebugName("GUI font atlas")),
                        "font atlas");
        auto upload = required(device.createCommandList(), "font upload");
        upload->open();
        upload->writeTexture(font, 0, 0, pixels, static_cast<std::size_t>(width) * 4);
        upload->close();
        device.executeCommandList(upload);
        fonts.SetTexID(1);
        sampler = required(device.createSampler(nvrhi::SamplerDesc()), "font sampler");
        vertex_shader = required(
            device.createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
                                shaders::gui_vert_spv, sizeof(shaders::gui_vert_spv)),
            "vertex shader");
        pixel_shader = required(
            device.createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
                                shaders::gui_frag_spv, sizeof(shaders::gui_frag_spv)),
            "pixel shader");
        const std::array attributes{nvrhi::VertexAttributeDesc()
                                        .setName("POSITION")
                                        .setFormat(nvrhi::Format::RG32_FLOAT)
                                        .setOffset(offsetof(ImDrawVert, pos))
                                        .setElementStride(sizeof(ImDrawVert)),
                                    nvrhi::VertexAttributeDesc()
                                        .setName("TEXCOORD")
                                        .setFormat(nvrhi::Format::RG32_FLOAT)
                                        .setOffset(offsetof(ImDrawVert, uv))
                                        .setElementStride(sizeof(ImDrawVert)),
                                    nvrhi::VertexAttributeDesc()
                                        .setName("COLOR")
                                        .setFormat(nvrhi::Format::RGBA8_UNORM)
                                        .setOffset(offsetof(ImDrawVert, col))
                                        .setElementStride(sizeof(ImDrawVert))};
        input_layout = required(device.createInputLayout(attributes.data(), 3, vertex_shader),
                                "vertex layout");
        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::AllGraphics;
        layout.bindings = {nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Sampler(0),
                           nvrhi::BindingLayoutItem::PushConstants(0, 16)};
        binding_layout = required(device.createBindingLayout(layout), "binding layout");
        nvrhi::BindingSetDesc set;
        set.bindings = {nvrhi::BindingSetItem::Texture_SRV(0, font),
                        nvrhi::BindingSetItem::Sampler(0, sampler),
                        nvrhi::BindingSetItem::PushConstants(0, 16)};
        bindings = required(device.createBindingSet(set, binding_layout), "font bindings");
    }
    void draw(const RenderFrame& frame, const ImDrawData& data) {
        if (data.TotalVtxCount == 0 || data.DisplaySize.x <= 0 || data.DisplaySize.y <= 0)
            return;
        if (data.TotalVtxCount < 0 || data.TotalIdxCount < 0 || data.TotalVtxCount > 4000000 ||
            data.TotalIdxCount > 12000000)
            throw std::runtime_error("GUI geometry exceeds frame limit");
        if (!pipeline) {
            nvrhi::GraphicsPipelineDesc desc;
            desc.VS = vertex_shader;
            desc.PS = pixel_shader;
            desc.inputLayout = input_layout;
            desc.bindingLayouts = {binding_layout};
            desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
            desc.renderState.rasterState.setCullNone().setScissorEnable(true);
            desc.renderState.blendState.targets[0]
                .enableBlend()
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
            pipeline = required(
                device.createGraphicsPipeline(desc, frame.framebuffer->getFramebufferInfo()),
                "pipeline");
        }
        auto& slot = slots[frame.slot];
        const auto vertex_bytes = static_cast<std::size_t>(data.TotalVtxCount) * sizeof(ImDrawVert);
        const auto index_bytes = static_cast<std::size_t>(data.TotalIdxCount) * sizeof(ImDrawIdx);
        // NVRHI's Vulkan writeBuffer rounds short uploads up to four bytes for
        // vkCmdUpdateBuffer. Supply initialized padding when 16-bit indices have
        // an odd count, so the driver cannot read beyond the source allocation.
        const auto index_upload_bytes = (index_bytes + 3) & ~std::size_t{3};
        auto buffer = [&](nvrhi::BufferHandle& handle, std::size_t size, bool vertex) {
            if (handle && handle->getDesc().byteSize >= size)
                return;
            handle = required(
                device.createBuffer(
                    nvrhi::BufferDesc()
                        .setByteSize(size + 65536)
                        .setIsVertexBuffer(vertex)
                        .setIsIndexBuffer(!vertex)
                        .enableAutomaticStateTracking(vertex ? nvrhi::ResourceStates::VertexBuffer
                                                             : nvrhi::ResourceStates::IndexBuffer)
                        .setDebugName(vertex ? "GUI vertices" : "GUI indices")),
                "geometry buffer");
        };
        buffer(slot.vertices, vertex_bytes, true);
        buffer(slot.indices, index_upload_bytes, false);
        std::vector<ImDrawVert> vertices;
        std::vector<ImDrawIdx> indices;
        vertices.reserve(static_cast<std::size_t>(data.TotalVtxCount));
        indices.reserve(index_upload_bytes / sizeof(ImDrawIdx));
        for (const auto* list : data.CmdLists) {
            vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }
        indices.resize(index_upload_bytes / sizeof(ImDrawIdx), 0);
        auto* commands = frame.commands;
        commands->writeBuffer(slot.vertices, vertices.data(), vertex_bytes);
        commands->writeBuffer(slot.indices, indices.data(), index_upload_bytes);
        const std::array constants{2.0F / data.DisplaySize.x, -2.0F / data.DisplaySize.y,
                                   -1.0F - data.DisplayPos.x * 2.0F / data.DisplaySize.x,
                                   1.0F + data.DisplayPos.y * 2.0F / data.DisplaySize.y};
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = frame.framebuffer;
        state.bindings = {bindings};
        state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(slot.vertices)};
        state.indexBuffer = {
            slot.indices,
            sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT, 0};
        state.viewport.addViewport(
            nvrhi::Viewport(static_cast<float>(frame.width), static_cast<float>(frame.height)));
        std::uint32_t vertex_offset = 0, index_offset = 0;
        for (const auto* list : data.CmdLists) {
            for (const auto& draw : list->CmdBuffer) {
                if (draw.UserCallback) {
                    if (draw.UserCallback != ImDrawCallback_ResetRenderState)
                        throw std::runtime_error("custom GUI draw callbacks are unsupported");
                    continue; // Every draw below rebinds the complete state.
                }
                if (draw.GetTexID() != 1)
                    throw std::runtime_error("unknown GUI texture");
                const auto clip = draw.ClipRect;
                const auto scale = data.FramebufferScale;
                const auto offset = data.DisplayPos;
                const auto x1 = std::clamp((clip.x - offset.x) * scale.x, 0.0F,
                                           static_cast<float>(frame.width));
                const auto x2 = std::clamp((clip.z - offset.x) * scale.x, 0.0F,
                                           static_cast<float>(frame.width));
                const auto y1 = std::clamp((clip.y - offset.y) * scale.y, 0.0F,
                                           static_cast<float>(frame.height));
                const auto y2 = std::clamp((clip.w - offset.y) * scale.y, 0.0F,
                                           static_cast<float>(frame.height));
                if (x2 <= x1 || y2 <= y1)
                    continue;
                state.viewport.scissorRects = {
                    nvrhi::Rect(static_cast<int>(x1), static_cast<int>(x2), static_cast<int>(y1),
                                static_cast<int>(y2))};
                commands->setGraphicsState(state);
                commands->setPushConstants(constants.data(), sizeof(constants));
                commands->drawIndexed(nvrhi::DrawArguments()
                                          .setVertexCount(draw.ElemCount)
                                          .setStartIndexLocation(index_offset + draw.IdxOffset)
                                          .setStartVertexLocation(vertex_offset + draw.VtxOffset));
            }
            vertex_offset += static_cast<std::uint32_t>(list->VtxBuffer.Size);
            index_offset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        }
    }
};
GuiRenderer::GuiRenderer(nvrhi::IDevice& device, ImFontAtlas& fonts)
    : impl_(std::make_unique<Impl>(device)) {
    impl_->initialize(fonts);
}
GuiRenderer::~GuiRenderer() = default;
void GuiRenderer::draw(const RenderFrame& frame, const ImDrawData& data) {
    impl_->draw(frame, data);
}
} // namespace vsim
