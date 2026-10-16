/* vktest.nro: the smallest check that Mesa's NVK works on the console, before the game.
   Makes a Vulkan device and a swapchain on libnx's default window, and clears the screen to a
   colour that changes for 10 seconds (or until + is pressed). Each step is written to
   sdmc:/switch/cbfd/vktest_log.txt. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_VI_NN
#include <vulkan/vulkan.h>

/* The statically linked driver's entry point. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

static FILE* log_file;

static void log_line(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (log_file != NULL) {
        vfprintf(log_file, format, args);
        fputc('\n', log_file);
        fflush(log_file);
    }
    va_end(args);
}

#define CHECK(call) do { VkResult check_result = (call); log_line("%s -> %d", #call, (int)check_result); \
    if (check_result != VK_SUCCESS) { goto done; } } while (0)

#define LOAD(instance, name) PFN_##name name = (PFN_##name)vk_icdGetInstanceProcAddr(instance, #name); \
    if (name == NULL) { log_line("no %s", #name); goto done; }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/cbfd", 0777);
    log_file = fopen("sdmc:/switch/cbfd/vktest_log.txt", "w");
    log_line("vktest: NVK clear-screen check");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    LOAD(VK_NULL_HANDLE, vkCreateInstance);
    const char* instance_extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME };
    VkApplicationInfo app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "vktest",
        .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo instance_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info, .enabledExtensionCount = 2, .ppEnabledExtensionNames = instance_extensions };
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    LOAD(instance, vkEnumeratePhysicalDevices);
    LOAD(instance, vkGetPhysicalDeviceProperties);
    LOAD(instance, vkGetPhysicalDeviceFeatures);
    LOAD(instance, vkCreateDevice);
    LOAD(instance, vkGetDeviceProcAddr);
    LOAD(instance, vkCreateViSurfaceNN);
    LOAD(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);

    uint32_t gpu_count = 1;
    VkPhysicalDevice gpu;
    CHECK(vkEnumeratePhysicalDevices(instance, &gpu_count, &gpu));
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(gpu, &properties);
    log_line("GPU: %s, Vulkan %u.%u.%u, driver 0x%X", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
        VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion), properties.driverVersion);
    log_line("maxPerStageDescriptorUpdateAfterBindSampledImages: see limits; maxImageDimension2D %u, maxBoundDescriptorSets %u",
        properties.limits.maxImageDimension2D, properties.limits.maxBoundDescriptorSets);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(gpu, &features);
    log_line("features: dualSrcBlend %u, depthClamp %u, independentBlend %u, samplerAnisotropy %u, sampleRateShading %u",
        features.dualSrcBlend, features.depthClamp, features.independentBlend, features.samplerAnisotropy, features.sampleRateShading);

    VkViSurfaceCreateInfoNN surface_info = { .sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN, .window = nwindowGetDefault() };
    VkSurfaceKHR surface;
    CHECK(vkCreateViSurfaceNN(instance, &surface_info, NULL, &surface));

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
        .queueCount = 1, .pQueuePriorities = &priority };
    const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info, .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions };
    CHECK(vkCreateDevice(gpu, &device_info, NULL, &device));

#define LOADD(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(device, #name); \
    if (name == NULL) { log_line("no %s", #name); goto done; }
    LOADD(vkGetDeviceQueue);
    LOADD(vkCreateSwapchainKHR);
    LOADD(vkGetSwapchainImagesKHR);
    LOADD(vkAcquireNextImageKHR);
    LOADD(vkQueuePresentKHR);
    LOADD(vkQueueSubmit);
    LOADD(vkCreateCommandPool);
    LOADD(vkAllocateCommandBuffers);
    LOADD(vkBeginCommandBuffer);
    LOADD(vkEndCommandBuffer);
    LOADD(vkCmdPipelineBarrier);
    LOADD(vkCmdClearColorImage);
    LOADD(vkCreateFence);
    LOADD(vkWaitForFences);
    LOADD(vkResetFences);
    LOADD(vkCreateSemaphore);
    LOADD(vkDeviceWaitIdle);

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSurfaceCapabilitiesKHR capabilities;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &capabilities));
    log_line("surface: %ux%u, images %u-%u, usage 0x%X, transforms 0x%X", capabilities.currentExtent.width,
        capabilities.currentExtent.height, capabilities.minImageCount, capabilities.maxImageCount,
        capabilities.supportedUsageFlags, capabilities.supportedTransforms);
    uint32_t format_count = 1;
    VkSurfaceFormatKHR format;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, &format);
    log_line("surface format %d", (int)format.format);

    VkSwapchainCreateInfoKHR swapchain_info = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
        .minImageCount = 3, .imageFormat = format.format, .imageColorSpace = format.colorSpace,
        .imageExtent = capabilities.currentExtent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    VkSwapchainKHR swapchain;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    uint32_t image_count = 8;
    VkImage images[8];
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));

    VkCommandPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = 0 };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo command_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore acquired, rendered;
    CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
    CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &rendered));

    const u64 start = armGetSystemTick();
    unsigned frames = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        const double seconds = (double)armTicksToNs(armGetSystemTick() - start) / 1e9;
        if (seconds > 10.0) {
            break;
        }
        uint32_t index;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            log_line("vkAcquireNextImageKHR -> %d at frame %u", (int)result, frames);
            break;
        }
        VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(command, &begin_info);
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier to_clear = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = images[index], .subresourceRange = range };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
            1, &to_clear);
        /* Red, then green, then blue, looping every 3 seconds. */
        const double phase = seconds / 3.0 - (int)(seconds / 3.0);
        VkClearColorValue colour = { .float32 = { phase < 1.0 / 3 ? 1.0f : 0.1f,
            phase >= 1.0 / 3 && phase < 2.0 / 3 ? 1.0f : 0.1f, phase >= 2.0 / 3 ? 1.0f : 0.1f, 1.0f } };
        vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
        VkImageMemoryBarrier to_present = to_clear;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0,
            NULL, 1, &to_present);
        vkEndCommandBuffer(command);
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acquired,
            .pWaitDstStageMask = &wait_stage, .commandBufferCount = 1, .pCommandBuffers = &command,
            .signalSemaphoreCount = 1, .pSignalSemaphores = &rendered };
        result = vkQueueSubmit(queue, 1, &submit, fence);
        if (result != VK_SUCCESS) {
            log_line("vkQueueSubmit -> %d at frame %u", (int)result, frames);
            break;
        }
        VkPresentInfoKHR present = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
            .pWaitSemaphores = &rendered, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index };
        result = vkQueuePresentKHR(queue, &present);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            log_line("vkQueuePresentKHR -> %d at frame %u", (int)result, frames);
            break;
        }
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);
        frames++;
        if (frames == 1 || frames % 60 == 0) {
            log_line("frame %u at %.2f s", frames, seconds);
        }
    }
    log_line("done: %u frames", frames);
    vkDeviceWaitIdle(device);

done:
    log_line("exit");
    if (log_file != NULL) {
        fclose(log_file);
    }
    return 0;
}
