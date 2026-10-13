#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
struct Platform {
    bool initialized{false};
    GLFWwindow* window{nullptr};
    VkInstance instance{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    ~Platform() {
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
        if (window)
            glfwDestroyWindow(window);
        if (initialized)
            glfwTerminate();
    }
};

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
    }
}
} // namespace

int main() {
    try {
        Platform platform;
        glfwSetErrorCallback([](int code, const char* text) {
            std::cerr << "GLFW " << code << ": " << text << '\n';
        });
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed; a desktop session is required");
        platform.initialized = true;
        if (!glfwVulkanSupported())
            throw std::runtime_error("no Vulkan loader/ICD available");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        platform.window = glfwCreateWindow(64, 64, "VSim platform probe", nullptr, nullptr);
        if (!platform.window)
            throw std::runtime_error("window creation failed");
        std::uint32_t extension_count = 0;
        const auto extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (!extensions)
            throw std::runtime_error("required Vulkan surface extensions are unavailable");
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "VSim platform probe";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = extension_count;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&info, nullptr, &platform.instance), "vkCreateInstance");
        check(
            glfwCreateWindowSurface(platform.instance, platform.window, nullptr, &platform.surface),
            "surface creation");
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(platform.instance, &count, nullptr), "device enumeration");
        if (count == 0) {
            throw std::runtime_error("no Vulkan physical devices found");
        }
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(platform.instance, &count, devices.data()),
              "device enumeration");
        devices.resize(count);
        for (const auto device : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3)
                continue;
            std::uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
            for (std::uint32_t index = 0; index < queue_count; ++index) {
                VkBool32 present = VK_FALSE;
                check(
                    vkGetPhysicalDeviceSurfaceSupportKHR(device, index, platform.surface, &present),
                    "presentation query");
                if (present && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    std::cout << "Vulkan 1.3 graphics/presentation queue available: "
                              << properties.deviceName << '\n';
                    return 0;
                }
            }
        }
        throw std::runtime_error("no Vulkan 1.3 graphics queue can present to the GLFW surface");
    } catch (const std::exception& error) {
        std::cerr << "vsim_platform_probe: " << error.what() << '\n';
        return 1;
    }
}
