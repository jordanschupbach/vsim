#include <vsim/mesh.hpp>
#include <vsim/renderer.hpp>
#include <vsim/renderer_extension.hpp>

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "overlay.hpp"
#include "scene_shaders.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vsim {
namespace {
constexpr std::uint32_t shadow_size = 2048;
constexpr std::size_t frames_in_flight = 2;

// Vulkan-Hpp's dispatcher used by this static NVRHI build belongs to one device.
// Reject overlapping owners instead of silently redirecting live device calls.
class RendererLease {
  public:
    RendererLease() {
        if (active_.test_and_set())
            throw std::logic_error("only one Renderer may be alive at a time");
    }
    ~RendererLease() {
        active_.clear();
    }
    RendererLease(const RendererLease&) = delete;
    RendererLease& operator=(const RendererLease&) = delete;

  private:
    inline static std::atomic_flag active_ = ATOMIC_FLAG_INIT;
};

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " +
                                 nvrhi::vulkan::resultToString(result));
}
template <typename T> T required(T resource, const char* label) {
    if (!resource)
        throw std::runtime_error(std::string("NVRHI failed to create ") + label);
    return resource;
}

class Diagnostics final : public nvrhi::IMessageCallback {
  public:
    std::atomic<unsigned int> errors{0};
    void message(nvrhi::MessageSeverity severity, const char* text) override {
        if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal)
            ++errors;
        std::cerr << "NVRHI: " << text << '\n';
    }
    static VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                   void* user) {
        auto& self = *static_cast<Diagnostics*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            ++self.errors;
        std::cerr << "Vulkan: " << data->pMessage << '\n';
        return VK_FALSE;
    }
};

struct GlfwLifetime {
    explicit GlfwLifetime(bool x11) {
        glfwSetErrorCallback([](int code, const char* text) {
            std::cerr << "GLFW " << code << ": " << text << '\n';
        });
        glfwInitHint(GLFW_PLATFORM, x11 ? GLFW_PLATFORM_X11 : GLFW_ANY_PLATFORM);
        if (!glfwInit())
            throw std::runtime_error(
                x11 ? "GLFW X11 initialization failed; an accessible X11 display is required"
                    : "GLFW initialization failed; run the editor/game in a desktop session");
    }
    ~GlfwLifetime() {
        glfwTerminate();
    }
    static std::shared_ptr<GlfwLifetime> acquire(bool x11) {
        // GLFW calls, including construction/destruction, are restricted to the main thread.
        static std::weak_ptr<GlfwLifetime> shared;
        auto instance = shared.lock();
        if (!instance) {
            instance = std::make_shared<GlfwLifetime>(x11);
            shared = instance;
        }
        return instance;
    }
};

struct VulkanContext {
    Diagnostics diagnostics;
    std::shared_ptr<GlfwLifetime> glfw;
    GLFWwindow* window{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queue_family{};
    VkSwapchainKHR swapchain{};
    VkFormat format{VK_FORMAT_B8G8R8A8_SRGB};
    VkExtent2D extent{};
    std::array<VkSemaphore, frames_in_flight> acquire{};
    std::vector<VkSemaphore> present;
    std::vector<VkImage> images;
    std::string name;
    std::vector<const char*> instance_extensions;
    std::vector<const char*> device_extensions;

    ~VulkanContext() {
        if (device) {
            (void)vkDeviceWaitIdle(device);
            clear_swapchain();
            for (auto semaphore : acquire)
                if (semaphore)
                    vkDestroySemaphore(device, semaphore, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy)
                destroy(instance, messenger, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
        if (window)
            glfwDestroyWindow(window);
    }
    void clear_swapchain() {
        for (auto semaphore : present)
            vkDestroySemaphore(device, semaphore, nullptr);
        present.clear();
        images.clear();
        if (swapchain)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    void initialize(const RendererConfig& config) {
        extent = {config.width, config.height};
        if (!config.offscreen) {
            glfw = GlfwLifetime::acquire(config.x11);
            if (!glfwVulkanSupported())
                throw std::runtime_error("GLFW cannot find a Vulkan loader/driver");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            window =
                glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                                 "Crystal Courier | VSim", nullptr, nullptr);
            if (!window)
                throw std::runtime_error("could not create game window");
            std::uint32_t count = 0;
            const auto extensions = glfwGetRequiredInstanceExtensions(&count);
            if (!extensions || count == 0)
                throw std::runtime_error("GLFW has no Vulkan surface extensions");
            instance_extensions.assign(extensions, extensions + count);
            device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }
        std::vector<const char*> layers;
        if (config.validation) {
            std::uint32_t count = 0;
            check(vkEnumerateInstanceLayerProperties(&count, nullptr),
                  "enumerate validation layers");
            std::vector<VkLayerProperties> available(count);
            check(vkEnumerateInstanceLayerProperties(&count, available.data()),
                  "enumerate validation layers");
            if (std::none_of(available.begin(), available.end(), [](const auto& layer) {
                    return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
                }))
                throw std::runtime_error("--validate requires VK_LAYER_KHRONOS_validation");
            layers.push_back("VK_LAYER_KHRONOS_validation");
            instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "Crystal Courier";
        application.pEngineName = "VSim";
        application.apiVersion = VK_API_VERSION_1_3;
        VkDebugUtilsMessengerCreateInfoEXT debug{};
        debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = Diagnostics::callback;
        debug.pUserData = &diagnostics;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions.size());
        info.ppEnabledExtensionNames = instance_extensions.data();
        info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        if (config.validation)
            info.pNext = &debug;
        check(vkCreateInstance(&info, nullptr, &instance), "create Vulkan 1.3 instance");
        if (config.validation) {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!create)
                throw std::runtime_error("Vulkan debug utils are unavailable");
            check(create(instance, &debug, nullptr, &messenger), "create Vulkan diagnostics");
        }
        if (window)
            check(glfwCreateWindowSurface(instance, window, nullptr, &surface),
                  "create window surface");
        select_device();
        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.synchronization2 = VK_TRUE;
        features13.maintenance4 = VK_TRUE;
        features13.dynamicRendering = VK_TRUE;
        features12.pNext = &features13;
        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.pNext = &features12;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
        device_info.ppEnabledExtensionNames = device_extensions.data();
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "create Vulkan device");
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        if (window) {
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            for (auto& semaphore : acquire)
                check(vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore),
                      "create acquire semaphore");
            create_swapchain();
        }
    }
    void select_device() {
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate Vulkan devices");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
              "enumerate Vulkan devices");
        int best = -1;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3)
                continue;
            VkPhysicalDeviceVulkan13Features features13{};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceVulkan12Features features12{};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.pNext = &features13;
            VkPhysicalDeviceFeatures2 features{};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features12;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features12.timelineSemaphore || !features13.synchronization2 ||
                !features13.maintenance4 || !features13.dynamicRendering)
                continue;
            std::uint32_t extension_count = 0;
            check(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count, nullptr),
                "enumerate device extensions");
            std::vector<VkExtensionProperties> extensions(extension_count);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extension_count,
                                                       extensions.data()),
                  "enumerate device extensions");
            if (surface && std::none_of(extensions.begin(), extensions.end(), [](const auto& e) {
                    return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                }))
                continue;
            std::uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queues.data());
            for (std::uint32_t i = 0; i < queue_count; ++i) {
                if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                    continue;
                VkBool32 supported = VK_TRUE;
                if (surface)
                    check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supported),
                          "query presentation support");
                if (!supported)
                    continue;
                const int rank = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100
                                 : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                                     ? 50
                                     : 1;
                if (rank > best) {
                    physical = candidate;
                    queue_family = i;
                    name = properties.deviceName;
                    best = rank;
                }
            }
        }
        if (!physical)
            throw std::runtime_error("no Vulkan 1.3 graphics device with timeline semaphores, "
                                     "synchronization2, maintenance4, and dynamic rendering");
    }
    void create_swapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities),
              "query surface capabilities");
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            throw std::runtime_error("surface does not support transfer destinations");
        std::uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr),
              "query surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()),
              "query surface formats");
        const auto chosen = std::find_if(formats.begin(), formats.end(), [](auto candidate) {
            return candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (chosen == formats.end())
            throw std::runtime_error("surface requires BGRA8 sRGB presentation support");
        format = chosen->format;
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            extent = capabilities.currentExtent;
        } else {
            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            extent.width =
                std::clamp(static_cast<std::uint32_t>(std::max(1, width)),
                           capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height =
                std::clamp(static_cast<std::uint32_t>(std::max(1, height)),
                           capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        std::uint32_t image_count = std::max(3U, capabilities.minImageCount);
        if (capabilities.maxImageCount)
            image_count = std::min(image_count, capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = image_count;
        info.imageFormat = format;
        info.imageColorSpace = chosen->colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & info.compositeAlpha)) {
            for (auto mode :
                 {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
                if (capabilities.supportedCompositeAlpha & mode) {
                    info.compositeAlpha = mode;
                    break;
                }
        }
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "create swapchain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr),
              "get swapchain images");
        images.resize(image_count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()),
              "get swapchain images");
        present.resize(image_count);
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (auto& semaphore : present)
            check(vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore),
                  "create presentation semaphore");
    }
};

struct FrameData {
    glm::mat4 view_projection;
    glm::mat4 light_view_projection;
    glm::vec4 camera;
    glm::vec4 sun_direction;
    glm::vec4 sun_color;
    glm::mat4 inverse_view_projection;
    glm::vec4 viewport_bounds;
};
struct ObjectData {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 material;
};
static_assert(sizeof(FrameData) == 256 && sizeof(ObjectData) == 96);

struct Frame {
    nvrhi::TextureHandle shadow, depth, hdr, ambient, normals, output;
    std::array<nvrhi::TextureHandle, 3> ao;
    std::array<nvrhi::FramebufferHandle, 3> ao_fb;
    nvrhi::BindingSetHandle ao_bindings;
    std::array<nvrhi::BindingSetHandle, 2> blur_bindings;
    nvrhi::FramebufferHandle shadow_fb, scene_fb, output_fb;
    nvrhi::BufferHandle constants, overlay;
    nvrhi::BindingSetHandle shadow_bindings, scene_bindings, tone_bindings;
    nvrhi::CommandListHandle command;
    nvrhi::EventQueryHandle completion;
    bool submitted{false};
};
struct Resources {
    nvrhi::ShaderHandle scene_vs, scene_ps, shadow_vs, fullscreen_vs, tone_ps, overlay_vs,
        overlay_ps, ao_ps, blur_ps;
    nvrhi::InputLayoutHandle input_layout, shadow_input_layout, overlay_input_layout;
    nvrhi::BufferHandle vertices;
    std::map<std::string, nvrhi::DrawArguments> meshes;
    nvrhi::BindingLayoutHandle shadow_layout, scene_layout, tone_layout, ao_layout, blur_layout;
    nvrhi::SamplerHandle shadow_sampler, linear_sampler, nearest_sampler;
    nvrhi::GraphicsPipelineHandle shadow_pipeline, scene_pipeline, tone_pipeline, overlay_pipeline,
        ao_pipeline, blur_pipeline;
    std::vector<Frame> frames;
    // Reused by capture(); sized with the frames.
    nvrhi::StagingTextureHandle readback;
    nvrhi::CommandListHandle readback_commands;
    std::vector<nvrhi::TextureHandle> swap_images;
    std::vector<bool> presented;
};

nvrhi::ViewportState viewport(std::uint32_t width, std::uint32_t height) {
    nvrhi::ViewportState result;
    result.addViewportAndScissorRect(
        nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height)));
    return result;
}
} // namespace

struct Renderer::Impl {
    RendererLease lease;
    RendererConfig config;
    VulkanContext vk;
    nvrhi::vulkan::DeviceHandle backend;
    nvrhi::DeviceHandle device;
    std::unique_ptr<Resources> resources;
    std::size_t frame_index{0};
    std::size_t last_frame{0};
    bool has_frame{false};
    bool recreate{false};
    std::array<std::string, 3> overlay;

    ~Impl() {
        if (device && !device->waitForIdle())
            std::cerr << "Vulkan device lost while shutting down\n";
        resources.reset();
        device = nullptr;
        backend = nullptr;
    }
    void initialize(RendererConfig options) {
        if (options.x11 && options.offscreen)
            throw std::invalid_argument(
                "X11 window selection is incompatible with offscreen rendering");
        if (options.width < 64 || options.height < 64 || options.width > 4096 ||
            options.height > 4096)
            throw std::invalid_argument("render dimensions must be in [64,4096]");
        options.ambient_occlusion.validate();
        config = options;
        vk.initialize(config);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vk.instance, vkGetInstanceProcAddr, vk.device);
        nvrhi::vulkan::DeviceDesc desc{};
        desc.errorCB = &vk.diagnostics;
        desc.instance = vk.instance;
        desc.physicalDevice = vk.physical;
        desc.device = vk.device;
        desc.graphicsQueue = vk.queue;
        desc.graphicsQueueIndex = static_cast<int>(vk.queue_family);
        desc.instanceExtensions = vk.instance_extensions.data();
        desc.numInstanceExtensions = vk.instance_extensions.size();
        desc.deviceExtensions = vk.device_extensions.data();
        desc.numDeviceExtensions = vk.device_extensions.size();
        backend = required(nvrhi::vulkan::createDevice(desc), "Vulkan backend");
        device = config.validation ? required(nvrhi::validation::createValidationLayer(backend),
                                              "validation layer")
                                   : nvrhi::DeviceHandle(backend);
        resources = std::make_unique<Resources>();
        create_resources();
    }
    void create_resources() {
        auto& r = *resources;
        auto shader = [this](nvrhi::ShaderType type, const auto& data) {
            return required(
                device->createShader(nvrhi::ShaderDesc().setShaderType(type), data, sizeof(data)),
                "shader");
        };
        r.scene_vs = shader(nvrhi::ShaderType::Vertex, shaders::scene_vert_spv);
        r.scene_ps = shader(nvrhi::ShaderType::Pixel, shaders::scene_frag_spv);
        r.shadow_vs = shader(nvrhi::ShaderType::Vertex, shaders::shadow_vert_spv);
        r.fullscreen_vs = shader(nvrhi::ShaderType::Vertex, shaders::fullscreen_vert_spv);
        r.tone_ps = shader(nvrhi::ShaderType::Pixel, shaders::tonemap_frag_spv);
        r.ao_ps = shader(nvrhi::ShaderType::Pixel, shaders::ssao_frag_spv);
        r.blur_ps = shader(nvrhi::ShaderType::Pixel, shaders::ssao_blur_frag_spv);
        r.overlay_vs = shader(nvrhi::ShaderType::Vertex, shaders::overlay_vert_spv);
        r.overlay_ps = shader(nvrhi::ShaderType::Pixel, shaders::overlay_frag_spv);
        const std::array overlay_attributes = {nvrhi::VertexAttributeDesc()
                                                   .setName("POSITION")
                                                   .setFormat(nvrhi::Format::RG32_FLOAT)
                                                   .setOffset(offsetof(OverlayVertex, position))
                                                   .setElementStride(sizeof(OverlayVertex)),
                                               nvrhi::VertexAttributeDesc()
                                                   .setName("COLOR")
                                                   .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                                   .setOffset(offsetof(OverlayVertex, color))
                                                   .setElementStride(sizeof(OverlayVertex))};
        r.overlay_input_layout =
            required(device->createInputLayout(overlay_attributes.data(), 2, r.overlay_vs),
                     "overlay vertex layout");
        const std::array attributes = {nvrhi::VertexAttributeDesc()
                                           .setName("POSITION")
                                           .setFormat(nvrhi::Format::RGB32_FLOAT)
                                           .setOffset(offsetof(Vertex, position))
                                           .setElementStride(sizeof(Vertex)),
                                       nvrhi::VertexAttributeDesc()
                                           .setName("NORMAL")
                                           .setFormat(nvrhi::Format::RGB32_FLOAT)
                                           .setOffset(offsetof(Vertex, normal))
                                           .setElementStride(sizeof(Vertex))};
        r.input_layout = required(
            device->createInputLayout(attributes.data(),
                                      static_cast<std::uint32_t>(attributes.size()), r.scene_vs),
            "vertex layout");
        r.shadow_input_layout = required(
            device->createInputLayout(attributes.data(), 1, r.shadow_vs), "shadow vertex layout");
        std::vector<Vertex> vertices;
        for (const auto* name : {"box", "sphere", "crystal"}) {
            const auto mesh = primitive_mesh(name);
            nvrhi::DrawArguments draw;
            draw.vertexCount = static_cast<std::uint32_t>(mesh.size());
            draw.startVertexLocation = static_cast<std::uint32_t>(vertices.size());
            r.meshes.emplace(name, draw);
            vertices.insert(vertices.end(), mesh.begin(), mesh.end());
        }
        auto vertex_desc = nvrhi::BufferDesc()
                               .setByteSize(vertices.size() * sizeof(Vertex))
                               .setIsVertexBuffer(true)
                               .setDebugName("Primitive vertices");
        r.vertices = required(device->createBuffer(vertex_desc), "vertex buffer");
        auto upload = required(device->createCommandList(), "upload commands");
        upload->open();
        upload->beginTrackingBufferState(r.vertices, nvrhi::ResourceStates::Common);
        upload->writeBuffer(r.vertices, vertices.data(), vertices.size() * sizeof(Vertex));
        upload->setPermanentBufferState(r.vertices, nvrhi::ResourceStates::VertexBuffer);
        upload->close();
        device->executeCommandList(upload);

        nvrhi::BindingLayoutDesc layout;
        layout.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
        layout.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::PushConstants(1, sizeof(ObjectData))};
        r.shadow_layout = required(device->createBindingLayout(layout), "shadow layout");
        layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(0));
        layout.bindings.push_back(nvrhi::BindingLayoutItem::Sampler(0));
        r.scene_layout = required(device->createBindingLayout(layout), "scene layout");
        layout.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Sampler(0),
            nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec4))};
        r.tone_layout = required(device->createBindingLayout(layout), "tonemap layout");
        layout.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                           nvrhi::BindingLayoutItem::PushConstants(1, sizeof(glm::vec4)),
                           nvrhi::BindingLayoutItem::Texture_SRV(0),
                           nvrhi::BindingLayoutItem::Texture_SRV(1),
                           nvrhi::BindingLayoutItem::Sampler(0)};
        r.ao_layout = required(device->createBindingLayout(layout), "SSAO layout");
        layout.bindings.push_back(nvrhi::BindingLayoutItem::Texture_SRV(2));
        r.blur_layout = required(device->createBindingLayout(layout), "SSAO blur layout");
        r.shadow_sampler = required(device->createSampler(nvrhi::SamplerDesc().setReductionType(
                                        nvrhi::SamplerReductionType::Comparison)),
                                    "shadow sampler");
        r.linear_sampler = required(device->createSampler(nvrhi::SamplerDesc()), "linear sampler");
        r.nearest_sampler = required(
            device->createSampler(nvrhi::SamplerDesc().setAllFilters(false)), "nearest sampler");
        create_targets();
        nvrhi::GraphicsPipelineDesc pipeline;
        pipeline.inputLayout = r.shadow_input_layout;
        pipeline.VS = r.shadow_vs;
        pipeline.bindingLayouts = {r.shadow_layout};
        // NVRHI uses a negative-height Vulkan viewport and expects unflipped projections.
        pipeline.renderState.rasterState.setCullBack()
            .setFrontCounterClockwise(true)
            .setDepthBias(2)
            .setSlopeScaleDepthBias(1.5F);
        r.shadow_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].shadow_fb->getFramebufferInfo()),
            "shadow pipeline");
        pipeline.VS = r.scene_vs;
        pipeline.PS = r.scene_ps;
        pipeline.inputLayout = r.input_layout;
        pipeline.bindingLayouts = {r.scene_layout};
        pipeline.renderState.rasterState.setDepthBias(0).setSlopeScaleDepthBias(0);
        r.scene_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].scene_fb->getFramebufferInfo()),
            "scene pipeline");
        pipeline = {};
        pipeline.VS = r.fullscreen_vs;
        pipeline.PS = r.tone_ps;
        pipeline.bindingLayouts = {r.tone_layout};
        pipeline.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
        pipeline.renderState.rasterState.setCullNone();
        r.tone_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].output_fb->getFramebufferInfo()),
            "tonemap pipeline");
        pipeline.PS = r.ao_ps;
        pipeline.bindingLayouts = {r.ao_layout};
        r.ao_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].ao_fb[0]->getFramebufferInfo()),
            "SSAO pipeline");
        pipeline.PS = r.blur_ps;
        pipeline.bindingLayouts = {r.blur_layout};
        r.blur_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].ao_fb[1]->getFramebufferInfo()),
            "SSAO blur pipeline");
        pipeline.VS = r.overlay_vs;
        pipeline.PS = r.overlay_ps;
        pipeline.inputLayout = r.overlay_input_layout;
        pipeline.bindingLayouts = {};
        r.overlay_pipeline = required(
            device->createGraphicsPipeline(pipeline, r.frames[0].output_fb->getFramebufferInfo()),
            "overlay pipeline");
    }
    nvrhi::TextureHandle target(const char* name, nvrhi::Format format, std::uint32_t width,
                                std::uint32_t height, nvrhi::ResourceStates state) {
        return required(device->createTexture(nvrhi::TextureDesc()
                                                  .setDebugName(name)
                                                  .setWidth(width)
                                                  .setHeight(height)
                                                  .setFormat(format)
                                                  .setIsRenderTarget(true)
                                                  .enableAutomaticStateTracking(state)),
                        name);
    }
    void create_targets() {
        auto& r = *resources;
        r.frames.clear();
        r.frames.resize(frames_in_flight);
        r.readback = nullptr;
        for (auto& frame : r.frames) {
            frame.shadow = target("Sun shadow", nvrhi::Format::D32, shadow_size, shadow_size,
                                  nvrhi::ResourceStates::ShaderResource);
            frame.depth = target("Scene depth", nvrhi::Format::D32, vk.extent.width,
                                 vk.extent.height, nvrhi::ResourceStates::DepthWrite);
            frame.hdr = target("HDR scene", nvrhi::Format::RGBA16_FLOAT, vk.extent.width,
                               vk.extent.height, nvrhi::ResourceStates::ShaderResource);
            frame.ambient = target("Indirect light", nvrhi::Format::RGBA16_FLOAT, vk.extent.width,
                                   vk.extent.height, nvrhi::ResourceStates::ShaderResource);
            frame.normals = target("World normals", nvrhi::Format::RGBA16_FLOAT, vk.extent.width,
                                   vk.extent.height, nvrhi::ResourceStates::ShaderResource);
            for (std::size_t index = 0; index < frame.ao.size(); ++index) {
                frame.ao[index] =
                    target("Ambient occlusion", nvrhi::Format::R16_FLOAT, vk.extent.width,
                           vk.extent.height, nvrhi::ResourceStates::ShaderResource);
                frame.ao_fb[index] =
                    required(device->createFramebuffer(
                                 nvrhi::FramebufferDesc().addColorAttachment(frame.ao[index])),
                             "SSAO framebuffer");
            }
            frame.output =
                target("Tone-mapped output", nvrhi::Format::SBGRA8_UNORM, vk.extent.width,
                       vk.extent.height, nvrhi::ResourceStates::CopySource);
            frame.shadow_fb =
                required(device->createFramebuffer(
                             nvrhi::FramebufferDesc().setDepthAttachment(frame.shadow)),
                         "shadow framebuffer");
            frame.scene_fb =
                required(device->createFramebuffer(nvrhi::FramebufferDesc()
                                                       .addColorAttachment(frame.hdr)
                                                       .addColorAttachment(frame.ambient)
                                                       .addColorAttachment(frame.normals)
                                                       .setDepthAttachment(frame.depth)),
                         "scene framebuffer");
            frame.output_fb =
                required(device->createFramebuffer(
                             nvrhi::FramebufferDesc().addColorAttachment(frame.output)),
                         "output framebuffer");
            auto constants = nvrhi::BufferDesc()
                                 .setByteSize(sizeof(FrameData))
                                 .setIsConstantBuffer(true)
                                 .setIsVolatile(true)
                                 .setMaxVersions(16)
                                 .setDebugName("Frame constants");
            frame.constants = required(device->createBuffer(constants), "frame constants");
            frame.overlay = required(
                device->createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(50000 * sizeof(OverlayVertex))
                                         .setIsVertexBuffer(true)
                                         .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                         .setKeepInitialState(true)
                                         .setDebugName("Overlay vertices")),
                "overlay vertices");
            nvrhi::BindingSetDesc bindings;
            bindings.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frame.constants),
                                 nvrhi::BindingSetItem::PushConstants(1, sizeof(ObjectData))};
            frame.shadow_bindings =
                required(device->createBindingSet(bindings, r.shadow_layout), "shadow bindings");
            bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(0, frame.shadow));
            bindings.bindings.push_back(nvrhi::BindingSetItem::Sampler(0, r.shadow_sampler));
            frame.scene_bindings =
                required(device->createBindingSet(bindings, r.scene_layout), "scene bindings");
            bindings.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frame.constants),
                                 nvrhi::BindingSetItem::PushConstants(1, sizeof(glm::vec4)),
                                 nvrhi::BindingSetItem::Texture_SRV(0, frame.depth),
                                 nvrhi::BindingSetItem::Texture_SRV(1, frame.normals),
                                 nvrhi::BindingSetItem::Sampler(0, r.nearest_sampler)};
            frame.ao_bindings =
                required(device->createBindingSet(bindings, r.ao_layout), "SSAO bindings");
            bindings.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(2, frame.ao[0]));
            frame.blur_bindings[0] = required(device->createBindingSet(bindings, r.blur_layout),
                                              "horizontal SSAO blur bindings");
            bindings.bindings.back() = nvrhi::BindingSetItem::Texture_SRV(2, frame.ao[1]);
            frame.blur_bindings[1] = required(device->createBindingSet(bindings, r.blur_layout),
                                              "vertical SSAO blur bindings");
            bindings.bindings = {nvrhi::BindingSetItem::Texture_SRV(0, frame.hdr),
                                 nvrhi::BindingSetItem::Texture_SRV(1, frame.ambient),
                                 nvrhi::BindingSetItem::Texture_SRV(2, frame.ao[2]),
                                 nvrhi::BindingSetItem::Sampler(0, r.linear_sampler),
                                 nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::vec4))};
            frame.tone_bindings =
                required(device->createBindingSet(bindings, r.tone_layout), "tonemap bindings");
            frame.command = required(device->createCommandList(), "frame command list");
            frame.completion = required(device->createEventQuery(), "frame completion query");
        }
        r.swap_images.clear();
        r.presented.assign(vk.images.size(), false);
        for (auto image : vk.images) {
            nvrhi::TextureDesc desc;
            desc.width = vk.extent.width;
            desc.height = vk.extent.height;
            desc.format = nvrhi::Format::SBGRA8_UNORM;
            desc.isShaderResource = false;
            r.swap_images.push_back(
                required(device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
                                                              nvrhi::Object(image), desc),
                         "swapchain image"));
        }
        has_frame = false;
    }
    void resize_targets() {
        if (!device->waitForIdle())
            throw std::runtime_error("device lost during resize");
        resources->swap_images.clear();
        if (vk.window) {
            vk.clear_swapchain();
            vk.create_swapchain();
        }
        create_targets();
        recreate = false;
    }
    FrameData frame_data(const Scene& scene, const RenderView* override_view) const {
        const Entity* camera = nullptr;
        for (auto id : scene.entities())
            if (scene.get(id).camera) {
                camera = &scene.get(id);
                break;
            }
        if (!camera && !override_view)
            throw std::invalid_argument("rendered scene needs a camera entity");
        FrameData data{};
        const auto& transform = override_view ? override_view->transform : camera->transform;
        const auto& lens = override_view ? override_view->camera : *camera->camera;
        const auto bounds = override_view ? override_view->bounds : glm::vec4(0, 0, 1, 1);
        const auto view = glm::mat4_cast(glm::conjugate(transform.rotation)) *
                          glm::translate(glm::mat4(1), -transform.position);
        auto camera_projection = lens.projection(static_cast<float>(vk.extent.width) * bounds.z /
                                                 (static_cast<float>(vk.extent.height) * bounds.w));
        camera_projection[1][1] *=
            -1; // Camera::projection targets a positive-height Vulkan viewport.
        data.view_projection = camera_projection * view;
        data.inverse_view_projection = glm::inverse(data.view_projection);
        data.viewport_bounds = bounds;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(data.view_projection[column][row]) ||
                    !std::isfinite(data.inverse_view_projection[column][row]))
                    throw std::invalid_argument("camera exceeds renderer projection precision");
        data.camera = glm::vec4(transform.position, 1);
        data.sun_direction = glm::vec4(glm::normalize(glm::vec3(-0.5F, 0.85F, 0.4F)), 0);
        data.sun_color = {3.8F, 3.5F, 3.0F, 1};
        auto projection = glm::orthoRH_ZO(-18.0F, 18.0F, -18.0F, 18.0F, 0.1F, 70.0F);
        data.light_view_projection =
            projection *
            glm::lookAtRH(glm::vec3(data.sun_direction) * 35.0F, glm::vec3(0), glm::vec3(0, 1, 0));
        return data;
    }
    void draw_entities(nvrhi::ICommandList* commands, const Scene& scene) {
        for (auto id : scene.entities()) {
            const auto& entity = scene.get(id);
            if (!entity.mesh)
                continue;
            const auto& mesh = *entity.mesh;
            const ObjectData object{entity.transform.matrix(),
                                    glm::vec4(mesh.color, 1),
                                    {mesh.metallic, mesh.roughness, mesh.emissive, 0}};
            commands->setPushConstants(&object, sizeof(object));
            commands->draw(resources->meshes.at(mesh.primitive));
        }
    }
    bool draw(const Scene& scene, const RenderView* view,
              const std::function<void(const RenderFrame&)>& extension) {
        if (view)
            view->validate();
        if (vk.window) {
            int width = 0, height = 0;
            glfwGetFramebufferSize(vk.window, &width, &height);
            if (width == 0 || height == 0) {
                glfwWaitEventsTimeout(0.05);
                return false;
            }
            if (recreate || static_cast<std::uint32_t>(width) != vk.extent.width ||
                static_cast<std::uint32_t>(height) != vk.extent.height)
                resize_targets();
        }
        const auto data = frame_data(scene, view);
        auto& r = *resources;
        auto& frame = r.frames[frame_index];
        if (frame.submitted) {
            device->waitEventQuery(frame.completion);
            device->resetEventQuery(frame.completion);
        }
        device->runGarbageCollection();
        std::uint32_t image = 0;
        if (vk.window) {
            const auto result = vkAcquireNextImageKHR(
                vk.device, vk.swapchain, std::numeric_limits<std::uint64_t>::max(),
                vk.acquire[frame_index], VK_NULL_HANDLE, &image);
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                recreate = true;
                return false;
            }
            if (result == VK_SUBOPTIMAL_KHR)
                recreate = true;
            else
                check(result, "acquire swapchain image");
        }
        auto& commands = frame.command;
        commands->open();
        commands->writeBuffer(frame.constants, &data, sizeof(data));
        commands->clearDepthStencilTexture(frame.shadow, nvrhi::AllSubresources, true, 1, false, 0);
        nvrhi::GraphicsState state;
        state.pipeline = r.shadow_pipeline;
        state.framebuffer = frame.shadow_fb;
        state.bindings = {frame.shadow_bindings};
        state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(r.vertices)};
        state.viewport = viewport(shadow_size, shadow_size);
        commands->setGraphicsState(state);
        draw_entities(commands, scene);
        commands->clearDepthStencilTexture(frame.depth, nvrhi::AllSubresources, true, 1, false, 0);
        commands->clearTextureFloat(frame.hdr, nvrhi::AllSubresources, nvrhi::Color(0, 0, 0, 0));
        commands->clearTextureFloat(frame.ambient, nvrhi::AllSubresources,
                                    nvrhi::Color(0, 0, 0, 0));
        commands->clearTextureFloat(frame.normals, nvrhi::AllSubresources,
                                    nvrhi::Color(0, 0, 0, 0));
        state.pipeline = r.scene_pipeline;
        state.framebuffer = frame.scene_fb;
        state.bindings = {frame.scene_bindings};
        state.viewport = viewport(vk.extent.width, vk.extent.height);
        if (view) {
            const auto b = view->bounds;
            const auto width = static_cast<float>(vk.extent.width);
            const auto height = static_cast<float>(vk.extent.height);
            state.viewport = {};
            state.viewport.addViewportAndScissorRect(nvrhi::Viewport(
                b.x * width, (b.x + b.z) * width, b.y * height, (b.y + b.w) * height, 0, 1));
        }
        commands->setGraphicsState(state);
        draw_entities(commands, scene);
        state = {};
        state.viewport = viewport(vk.extent.width, vk.extent.height);
        const auto& ao = config.ambient_occlusion;
        if (ao.enabled && ao.strength > 0) {
            state.pipeline = r.ao_pipeline;
            state.framebuffer = frame.ao_fb[0];
            state.bindings = {frame.ao_bindings};
            commands->setGraphicsState(state);
            glm::vec4 ao_settings(ao.radius, ao.bias, ao.strength, 0);
            commands->setPushConstants(&ao_settings, sizeof(ao_settings));
            commands->draw(nvrhi::DrawArguments().setVertexCount(3));
            state.pipeline = r.blur_pipeline;
            for (std::size_t pass = 0; pass < 2; ++pass) {
                state.framebuffer = frame.ao_fb[pass + 1];
                state.bindings = {frame.blur_bindings[pass]};
                commands->setGraphicsState(state);
                ao_settings.w = static_cast<float>(pass);
                commands->setPushConstants(&ao_settings, sizeof(ao_settings));
                commands->draw(nvrhi::DrawArguments().setVertexCount(3));
            }
        } else {
            commands->clearTextureFloat(frame.ao[2], nvrhi::AllSubresources, nvrhi::Color(1));
        }
        state.pipeline = r.tone_pipeline;
        state.framebuffer = frame.output_fb;
        state.bindings = {frame.tone_bindings};
        state.viewport = viewport(vk.extent.width, vk.extent.height);
        commands->setGraphicsState(state);
        const glm::vec4 settings(1.15F, 0, ao.visualize ? 1.0F : 0.0F, 0);
        commands->setPushConstants(&settings, sizeof(settings));
        commands->draw(nvrhi::DrawArguments().setVertexCount(3));
        const auto overlay_data = overlay_vertices(vk.extent.width, vk.extent.height, overlay);
        if (!overlay_data.empty()) {
            if (overlay_data.size() > 50000)
                throw std::runtime_error("overlay vertex capacity exceeded");
            commands->writeBuffer(frame.overlay, overlay_data.data(),
                                  overlay_data.size() * sizeof(OverlayVertex));
            state.pipeline = r.overlay_pipeline;
            state.bindings = {};
            state.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(frame.overlay)};
            commands->setGraphicsState(state);
            commands->draw(nvrhi::DrawArguments().setVertexCount(
                static_cast<std::uint32_t>(overlay_data.size())));
        }
        if (extension)
            extension({commands, frame.output_fb, frame_index, vk.extent.width, vk.extent.height});
        if (vk.window) {
            commands->beginTrackingTextureState(r.swap_images[image], nvrhi::AllSubresources,
                                                r.presented[image] ? nvrhi::ResourceStates::Present
                                                                   : nvrhi::ResourceStates::Common);
            commands->copyTexture(r.swap_images[image], nvrhi::TextureSlice(), frame.output,
                                  nvrhi::TextureSlice());
            commands->setTextureState(r.swap_images[image], nvrhi::AllSubresources,
                                      nvrhi::ResourceStates::Present);
            commands->commitBarriers();
        }
        commands->close();
        if (vk.window) {
            backend->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, vk.acquire[frame_index],
                                           0);
            backend->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, vk.present[image], 0);
        }
        device->executeCommandList(commands);
        device->setEventQuery(frame.completion, nvrhi::CommandQueue::Graphics);
        frame.submitted = true;
        if (vk.window) {
            VkPresentInfoKHR present{};
            present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &vk.present[image];
            present.swapchainCount = 1;
            present.pSwapchains = &vk.swapchain;
            present.pImageIndices = &image;
            const auto result = vkQueuePresentKHR(vk.queue, &present);
            if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
                recreate = true;
            else
                check(result, "present frame");
            r.presented[image] = true;
        }
        last_frame = frame_index;
        has_frame = true;
        frame_index = (frame_index + 1) % frames_in_flight;
        if (vk.diagnostics.errors.load() != 0)
            throw std::runtime_error("rendering failed validation; see diagnostics above");
        return true;
    }
};

Renderer::Renderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    impl_->initialize(config);
}
Renderer::~Renderer() = default;
bool Renderer::should_close() const {
    return impl_->vk.window && glfwWindowShouldClose(impl_->vk.window);
}
InputFrame Renderer::poll_input() {
    auto* window = impl_->vk.window;
    if (!window)
        return {};
    glfwPollEvents();
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (!glfwGetWindowAttrib(window, GLFW_FOCUSED))
        return {};
    auto held = [window](int key) { return glfwGetKey(window, key) == GLFW_PRESS ? 1.0F : 0.0F; };
    return {{"move_x", std::clamp(held(GLFW_KEY_D) + held(GLFW_KEY_RIGHT) - held(GLFW_KEY_A) -
                                      held(GLFW_KEY_LEFT),
                                  -1.0F, 1.0F)},
            {"move_z", std::clamp(held(GLFW_KEY_S) + held(GLFW_KEY_DOWN) - held(GLFW_KEY_W) -
                                      held(GLFW_KEY_UP),
                                  -1.0F, 1.0F)},
            {"jump", held(GLFW_KEY_SPACE)},
            {"restart", held(GLFW_KEY_R)}};
}
void Renderer::set_title(const std::string& title) {
    if (impl_->vk.window)
        glfwSetWindowTitle(impl_->vk.window, title.c_str());
}
void Renderer::set_overlay(std::string title, std::string status, std::string controls) {
    if (title.size() > 128 || status.size() > 128 || controls.size() > 128)
        throw std::invalid_argument("overlay lines are limited to 128 bytes");
    impl_->overlay = {std::move(title), std::move(status), std::move(controls)};
}
void AmbientOcclusion::validate() const {
    if (!std::isfinite(radius) || radius < 0.01F || radius > 100 || !std::isfinite(bias) ||
        bias < 0 || bias >= radius || !std::isfinite(strength) || strength < 0 || strength > 4)
        throw std::invalid_argument(
            "SSAO requires radius [0.01,100], bias [0,radius), strength [0,4]");
}
void Renderer::set_ambient_occlusion(const AmbientOcclusion& settings) {
    settings.validate();
    impl_->config.ambient_occlusion = settings;
}
bool Renderer::draw(const Scene& scene, const RenderView* view,
                    const std::function<void(const RenderFrame&)>& overlay) {
    return impl_->draw(scene, view, overlay);
}
GLFWwindow* Renderer::window() const noexcept {
    return impl_->vk.window;
}
nvrhi::IDevice* Renderer::graphics_device() const noexcept {
    return impl_->device;
}
void Renderer::wait_idle() {
    if (!impl_->device->waitForIdle())
        throw std::runtime_error("device lost while waiting for rendering");
}
void RenderView::validate() const {
    transform.validate();
    camera.validate();
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(bounds[i]))
            throw std::invalid_argument("viewport bounds must be finite");
    if (bounds.x < 0 || bounds.y < 0 || bounds.z <= 0 || bounds.w <= 0 || bounds.x + bounds.z > 1 ||
        bounds.y + bounds.w > 1)
        throw std::invalid_argument("viewport must lie within the output image");
}
void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width < 64 || height < 64 || width > 4096 || height > 4096)
        throw std::invalid_argument("render dimensions must be in [64,4096]");
    if (impl_->vk.window) {
        glfwSetWindowSize(impl_->vk.window, static_cast<int>(width), static_cast<int>(height));
        impl_->recreate = true;
    } else {
        impl_->vk.extent = {width, height};
        impl_->resize_targets();
    }
}
Image Renderer::capture() {
    Image image{0, 0, {}};
    capture(image);
    return image;
}
void Renderer::capture(Image& image) {
    if (!impl_->has_frame)
        throw std::logic_error("render a frame before capturing it");
    auto& r = *impl_->resources;
    auto& frame = r.frames[impl_->last_frame];
    auto& device = impl_->device;
    if (!r.readback)
        r.readback = required(
            device->createStagingTexture(frame.output->getDesc(), nvrhi::CpuAccessMode::Read),
            "readback texture");
    if (!r.readback_commands)
        r.readback_commands = required(device->createCommandList(), "readback commands");
    auto& commands = r.readback_commands;
    commands->open();
    commands->copyTexture(r.readback, nvrhi::TextureSlice(), frame.output, nvrhi::TextureSlice());
    commands->close();
    device->executeCommandList(commands);
    if (!device->waitForIdle())
        throw std::runtime_error("device lost during capture");
    const auto width = impl_->vk.extent.width, height = impl_->vk.extent.height;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    std::size_t pitch = 0;
    const auto* mapped = static_cast<const std::uint8_t*>(device->mapStagingTexture(
        r.readback, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
    if (!mapped)
        throw std::runtime_error("could not map captured image");
    // BGRA to RGBA, one 32-bit pixel at a time (the loop vectorizes).
    static_assert(std::endian::native == std::endian::little);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* source = mapped + static_cast<std::size_t>(y) * pitch;
        auto* destination = image.rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, source + static_cast<std::size_t>(x) * 4, 4);
            pixel = (pixel & 0xFF00FF00U) | ((pixel >> 16) & 0xFFU) | ((pixel & 0xFFU) << 16);
            std::memcpy(destination + static_cast<std::size_t>(x) * 4, &pixel, 4);
        }
    }
    device->unmapStagingTexture(r.readback);
    if (impl_->vk.diagnostics.errors.load() != 0)
        throw std::runtime_error("capture failed validation");
}
std::string Renderer::device_name() const {
    return impl_->vk.name;
}
unsigned int Renderer::validation_errors() const {
    return impl_->vk.diagnostics.errors.load();
}
void Image::write_ppm(const std::filesystem::path& path) const {
    if (width == 0 || height == 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / height / 4 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4)
        throw std::invalid_argument("invalid image storage");
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot create screenshot: " + path.string());
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        stream.write(reinterpret_cast<const char*>(rgba.data() + i), 3);
    stream.close();
    if (!stream)
        throw std::runtime_error("failed to write screenshot");
}
} // namespace vsim
