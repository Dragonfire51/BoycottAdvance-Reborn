/*
 * vk.c - native Vulkan presentation backend.
 *
 * Deliberately shader-free: the composed ARGB image is copied straight into
 * the acquired swapchain image with vkCmdCopyBufferToImage, then presented.
 * That avoids SPIR-V shaders and pipeline state entirely while still giving a
 * true Vulkan swapchain path. Drawn only when the build has the Vulkan SDK.
 */
#include "video.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SDL_Window      *win;
    VkInstance       inst;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         dev;
    VkQueue          queue;
    uint32_t         qfam;
    VkSwapchainKHR   swap;
    VkFormat         fmt;
    VkExtent2D       extent;
    uint32_t         img_count;
    VkImage         *images;
    VkBuffer         stage;
    VkDeviceMemory   stage_mem;
    void            *mapped;
    size_t           stage_bytes;
    VkCommandPool    pool;
    VkCommandBuffer  cmd;
    VkSemaphore      sem_img, sem_draw;
    VkFence          fence;
    int              w, h;
} Vk;

void vk_destroy(void *state);

static uint32_t pick_memtype(Vk *v, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(v->phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

static int create_stage(Vk *v, size_t bytes) {
    if (v->stage) {
        vkUnmapMemory(v->dev, v->stage_mem);
        vkDestroyBuffer(v->dev, v->stage, NULL);
        vkFreeMemory(v->dev, v->stage_mem, NULL);
        v->stage = VK_NULL_HANDLE;
    }
    VkBufferCreateInfo bi; memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(v->dev, &bi, NULL, &v->stage) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(v->dev, v->stage, &mr);
    VkMemoryAllocateInfo ai; memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = pick_memtype(v, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(v->dev, &ai, NULL, &v->stage_mem) != VK_SUCCESS) return -1;
    vkBindBufferMemory(v->dev, v->stage, v->stage_mem, 0);
    vkMapMemory(v->dev, v->stage_mem, 0, bytes, 0, &v->mapped);
    v->stage_bytes = bytes;
    return 0;
}

static int create_swapchain(Vk *v) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(v->phys, v->surface, &caps);

    uint32_t nfmt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(v->phys, v->surface, &nfmt, NULL);
    VkSurfaceFormatKHR *fmts = malloc(sizeof(VkSurfaceFormatKHR) * (nfmt ? nfmt : 1));
    vkGetPhysicalDeviceSurfaceFormatsKHR(v->phys, v->surface, &nfmt, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < nfmt; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
            chosen = fmts[i]; break;
        }
    free(fmts);

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {   /* size is driven by the window */
        int ww = 0, wh = 0;
        SDL_GetWindowSize(v->win, &ww, &wh);
        ext.width = (uint32_t)ww; ext.height = (uint32_t)wh;
        if (ext.width < caps.minImageExtent.width)  ext.width = caps.minImageExtent.width;
        if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
        if (ext.width > caps.maxImageExtent.width)  ext.width = caps.maxImageExtent.width;
        if (ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
    }
    VkSwapchainCreateInfoKHR ci; memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = v->surface;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(v->dev, &ci, NULL, &v->swap) != VK_SUCCESS) return -1;

    vkGetSwapchainImagesKHR(v->dev, v->swap, &v->img_count, NULL);
    v->images = malloc(sizeof(VkImage) * v->img_count);
    vkGetSwapchainImagesKHR(v->dev, v->swap, &v->img_count, v->images);
    v->fmt = chosen.format;
    v->extent = ext;
    return 0;
}

/* one-time full setup */
void *vk_create(void *winp) {
    SDL_Window *win = (SDL_Window *)winp;
    Vk *v = (Vk *)calloc(1, sizeof(Vk));
    v->win = win;

    unsigned nex = 0;
    SDL_Vulkan_GetInstanceExtensions(win, &nex, NULL);
    const char **exts = malloc(sizeof(char *) * nex);
    SDL_Vulkan_GetInstanceExtensions(win, &nex, exts);

    VkApplicationInfo app; memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "BoycottAdvance Reborn";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici; memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = nex;
    ici.ppEnabledExtensionNames = exts;
    if (vkCreateInstance(&ici, NULL, &v->inst) != VK_SUCCESS) { free(exts); vk_destroy(v); return NULL; }
    free(exts);

    if (!SDL_Vulkan_CreateSurface(win, v->inst, &v->surface)) { vk_destroy(v); return NULL; }

    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(v->inst, &ndev, NULL);
    if (!ndev) { vk_destroy(v); return NULL; }
    VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * ndev);
    vkEnumeratePhysicalDevices(v->inst, &ndev, devs);
    v->phys = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < ndev && !v->phys; i++) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
        VkQueueFamilyProperties *qp = malloc(sizeof(VkQueueFamilyProperties) * nq);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, v->surface, &present);
            if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { v->phys = devs[i]; v->qfam = q; break; }
        }
        free(qp);
    }
    free(devs);
    if (!v->phys) { vk_destroy(v); return NULL; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci; memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = v->qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char *dext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dci; memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &dext;
    if (vkCreateDevice(v->phys, &dci, NULL, &v->dev) != VK_SUCCESS) { vk_destroy(v); return NULL; }
    vkGetDeviceQueue(v->dev, v->qfam, 0, &v->queue);

    if (create_swapchain(v) != 0) { vk_destroy(v); return NULL; }

    VkCommandPoolCreateInfo pci; memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = v->qfam;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(v->dev, &pci, NULL, &v->pool) != VK_SUCCESS) { vk_destroy(v); return NULL; }
    VkCommandBufferAllocateInfo cai; memset(&cai, 0, sizeof(cai));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = v->pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(v->dev, &cai, &v->cmd);

    VkSemaphoreCreateInfo si; memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(v->dev, &si, NULL, &v->sem_img);
    vkCreateSemaphore(v->dev, &si, NULL, &v->sem_draw);
    VkFenceCreateInfo fi; memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(v->dev, &fi, NULL, &v->fence);

    v->w = (int)v->extent.width; v->h = (int)v->extent.height;
    if (create_stage(v, (size_t)v->w * v->h * 4) != 0) { vk_destroy(v); return NULL; }
    return v;
}

void vk_present(void *state, const uint32_t *argb, int w, int h) {
    Vk *v = (Vk *)state;
    if (!v) return;
    if (w != v->w || h != v->h) {
        vkDeviceWaitIdle(v->dev);
        create_stage(v, (size_t)w * h * 4);
        v->w = w; v->h = h;
    }
    memcpy(v->mapped, argb, (size_t)w * h * 4);

    vkWaitForFences(v->dev, 1, &v->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(v->dev, 1, &v->fence);

    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(v->dev, v->swap, UINT64_MAX, v->sem_img, VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(v->dev);
        vkDestroySwapchainKHR(v->dev, v->swap, NULL);
        free(v->images);
        if (create_swapchain(v) != 0) return;
        vkAcquireNextImageKHR(v->dev, v->swap, UINT64_MAX, v->sem_img, VK_NULL_HANDLE, &idx);
    } else if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
        return;
    }

    vkResetCommandBuffer(v->cmd, 0);
    VkCommandBufferBeginInfo bi; memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(v->cmd, &bi);

    VkImageMemoryBarrier b; memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = v->images[idx];
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(v->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);

    VkBufferImageCopy region; memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)w;
    region.imageExtent.height = (uint32_t)h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(v->cmd, v->stage, v->images[idx],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(v->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
    vkEndCommandBuffer(v->cmd);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo sub; memset(&sub, 0, sizeof(sub));
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.waitSemaphoreCount = 1;
    sub.pWaitSemaphores = &v->sem_img;
    sub.pWaitDstStageMask = &wait;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &v->cmd;
    sub.signalSemaphoreCount = 1;
    sub.pSignalSemaphores = &v->sem_draw;
    vkQueueSubmit(v->queue, 1, &sub, v->fence);

    VkPresentInfoKHR pi; memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &v->sem_draw;
    pi.swapchainCount = 1;
    pi.pSwapchains = &v->swap;
    pi.pImageIndices = &idx;
    vkQueuePresentKHR(v->queue, &pi);
}

void vk_destroy(void *state) {
    Vk *v = (Vk *)state;
    if (!v) return;
    if (v->dev) {
        vkDeviceWaitIdle(v->dev);
        if (v->mapped) vkUnmapMemory(v->dev, v->stage_mem);
        if (v->stage) vkDestroyBuffer(v->dev, v->stage, NULL);
        if (v->stage_mem) vkFreeMemory(v->dev, v->stage_mem, NULL);
        if (v->fence) vkDestroyFence(v->dev, v->fence, NULL);
        if (v->sem_img) vkDestroySemaphore(v->dev, v->sem_img, NULL);
        if (v->sem_draw) vkDestroySemaphore(v->dev, v->sem_draw, NULL);
        if (v->pool) vkDestroyCommandPool(v->dev, v->pool, NULL);
        if (v->swap) vkDestroySwapchainKHR(v->dev, v->swap, NULL);
        vkDestroyDevice(v->dev, NULL);
    }
    if (v->surface && v->inst) vkDestroySurfaceKHR(v->inst, v->surface, NULL);
    if (v->inst) vkDestroyInstance(v->inst, NULL);
    free(v->images);
    free(v);
}
