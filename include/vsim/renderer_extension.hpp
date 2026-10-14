#pragma once

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>

namespace vsim {
// Borrowed handles valid only inside Renderer::draw's overlay callback. The
// command list is open, after tonemapping and before presentation. Do not close
// or submit it. Buffers indexed by slot may be reused: its previous GPU work has
// completed. Extensions must survive until that work completes (wait_idle).
struct RenderFrame {
    nvrhi::ICommandList* commands;
    nvrhi::IFramebuffer* framebuffer;
    std::size_t slot;
    std::uint32_t width;
    std::uint32_t height;
};
} // namespace vsim
