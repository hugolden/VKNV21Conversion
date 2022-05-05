//
// Created by Hu Sichao on 4/30/2022.
//

#include <android/hardware_buffer_jni.h>
#include <android/log.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_android.h>

#include "NV21Converter.h"
#include "Utils.h"
#include <string>
#include <cmath>

using namespace sample;
using namespace std;

bool NV21Converter::initialize(AAssetManager* assetManager) {
    // initialize vulkan instance

    mAssetManager = assetManager;
    CALL_VK(vkEnumerateInstanceVersion, &mInstanceVersion);
    if (VK_VERSION_MAJOR(mInstanceVersion) != 1) {
        LOGE("incompatible Vulkan version");
        return false;
    }
    LOGV("Vulkan instance version: %d.%d", VK_VERSION_MAJOR(mInstanceVersion),
         VK_VERSION_MINOR(mInstanceVersion));

    std::vector<const char*> instanceLayers;

    std::vector<const char*> instanceExtensions = {
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

    const VkApplicationInfo applicationDesc = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "nv21_rgb_conversion",
            .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
            .apiVersion = static_cast<uint32_t>((VK_VERSION_MINOR(mInstanceVersion) >= 1)
                                                ? VK_API_VERSION_1_1
                                                : VK_API_VERSION_1_0),
    };

    const VkInstanceCreateInfo instanceDesc = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &applicationDesc,
            .enabledLayerCount = static_cast<uint32_t>(instanceLayers.size()),
            .ppEnabledLayerNames = instanceLayers.data(),
            .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
            .ppEnabledExtensionNames = instanceExtensions.data(),
    };


    CALL_VK(vkCreateInstance,&instanceDesc, nullptr,&mInstance);

    RET_CHECK(initDevice()
                && createPools()
                );

    initCommandBuffer();

    bindUniformLayout();
    return true;

}

bool NV21Converter::createPools() {
    mDescriptorPool = VkDescriptorPool(mDevice);
    const std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
            {
                    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    // We have three pipelines, each need 1 combined image sampler descriptor.
                    .descriptorCount = 3,
            },
            {
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    // We have three pipelines, each need 1 storage image descriptor.
                    .descriptorCount = 3,
            },
            {
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    // We have three pipelines, each need 1 uniform buffer.
                    .descriptorCount = 3,
            },
    };
    const VkDescriptorPoolCreateInfo descriptorPoolDesc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 3,
            .poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size()),
            .pPoolSizes = descriptorPoolSizes.data(),
    };
    CALL_VK(vkCreateDescriptorPool, mDevice, &descriptorPoolDesc, nullptr,
            &mDescriptorPool);

    // Create command pool
    mCommandPool = VkCommandPool(mDevice);
    const VkCommandPoolCreateInfo cmdpoolDesc = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mQueueFamilyIndex,
    };
    CALL_VK(vkCreateCommandPool, mDevice, &cmdpoolDesc, nullptr, &mCommandPool);
    return true;
}

bool NV21Converter::bindUniformLayout() {
    std::vector<VkDescriptorSetLayoutBinding> descriptorsetLayoutBinding = {
            {
                    .binding = 0,  // input image , layout = 0
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                    .binding = 1,  // output image , layout = 1
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },

    };

    const VkDescriptorSetLayoutCreateInfo descriptorsetLayoutDesc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<uint32_t>(descriptorsetLayoutBinding.size()),
            .pBindings = descriptorsetLayoutBinding.data(),
    };

    CALL_VK(vkCreateDescriptorSetLayout, mDevice, &descriptorsetLayoutDesc, nullptr,
            &mDescriptorSetLayout);

    const VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = mDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &mDescriptorSetLayout,
    };
    CALL_VK(vkAllocateDescriptorSets, mDevice, &descriptorSetAllocateInfo,
            &mDescriptorSet);

    return true;
}



void NV21Converter::configureFrameSize(uint32_t width, uint32_t height) {
    allocateDeviceMemory(width,height);
    createImageView();
    createYUVConversion();
    createSampler();
    bindShader(mAssetManager);
    mWidth = width;
    mHeight = height;
    mOutputSize = width*height;
    mInputSize = static_cast<uint32_t>(static_cast<float>(mOutputSize) * 1.5f);
}

void NV21Converter::feedBuffer(void *buffer_ptr) {
    copyToDeviceBuffer(buffer_ptr);
    submitCommand();
}

void NV21Converter::destroy() {

}

bool NV21Converter::initDevice() {
    uint32_t numDevices = 0;
    CALL_VK(vkEnumeratePhysicalDevices, mInstance, &numDevices, nullptr);
    std::vector<VkPhysicalDevice> devices(numDevices);
    CALL_VK(vkEnumeratePhysicalDevices, mInstance, &numDevices, devices.data());
    for (auto device : devices) {
        uint32_t numQueueFamilies = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &numQueueFamilies, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(numQueueFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &numQueueFamilies, queueFamilies.data());

        uint32_t qf = 0;
        bool haveQf = false;
        for (uint32_t i = 0; i < queueFamilies.size(); i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                qf = i;
                haveQf = true;
                break;
            }
        }
        if (!haveQf) continue;
        mPhysicalDevice = device;
        mQueueFamilyIndex = qf;
        break;
    }
    RET_CHECK(mPhysicalDevice != VK_NULL_HANDLE);
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &mPhysicalDeviceProperties);
    mWorkGroupSize = chooseWorkGroupSize(mPhysicalDeviceProperties.limits);
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mPhysicalDeviceMemoryProperties);
    LOGV("Using physical device '%s'", mPhysicalDeviceProperties.deviceName);

    std::vector<const char*> deviceExtensions = {
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
            VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
            VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
    };

    // Create logical device
    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueDesc = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = mQueueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
    };
    const VkDeviceCreateInfo deviceDesc = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueDesc,
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
            .ppEnabledExtensionNames = deviceExtensions.data(),
            .pEnabledFeatures = nullptr,
    };
    CALL_VK(vkCreateDevice, mPhysicalDevice, &deviceDesc, nullptr, &mDevice);
    vkGetDeviceQueue(mDevice, mQueueFamilyIndex, 0, &mQueue);

    return true;

}

uint32_t NV21Converter::chooseWorkGroupSize(const VkPhysicalDeviceLimits& limits) {
    // Use 64 as the baseline.
    uint32_t size = 64;

    // Make sure the size does not exceed the limit along the X and Y axis.
    size = std::min<uint32_t>(size, limits.maxComputeWorkGroupSize[0]);
    size = std::min<uint32_t>(size, limits.maxComputeWorkGroupSize[1]);

    // Make sure the total number of invocations does not exceed the limit.
    size = std::min<uint32_t>(
            size, static_cast<uint32_t>(std::sqrt(limits.maxComputeWorkGroupInvocations)));

    // We prefer the workgroup size to be a multiple of 4.
    size &= ~(3u);

    LOGV("maxComputeWorkGroupInvocations: %d, maxComputeWorkGroupSize: (%d, %d)",
         limits.maxComputeWorkGroupInvocations, limits.maxComputeWorkGroupSize[0],
         limits.maxComputeWorkGroupSize[1]);
    LOGV("Choose workgroup size: (%d, %d)", size, size);
    return size;
}

bool NV21Converter::initCommandBuffer() {
    const VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = mCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
    };
    CALL_VK(vkAllocateCommandBuffers, mDevice, &commandBufferAllocateInfo,
            &mCommandBuffer);
    return true;
}

std::optional<uint32_t> NV21Converter::findMemoryType(uint32_t memoryTypeBits,
                                       VkFlags properties){
    for (uint32_t i = 0; i < mPhysicalDeviceMemoryProperties.memoryTypeCount; i++) {
        if (memoryTypeBits & 1u) {
            if ((mPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
                return i;
            }
        }
        memoryTypeBits >>= 1u;
    }
    return std::nullopt;
}

bool NV21Converter::allocateDeviceMemory(uint32_t width,uint32_t height) {

    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    const VkImageCreateInfo imageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CALL_VK(vkCreateImage, mDevice, &imageCreateInfo, nullptr, &mImage);

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(mDevice, mImage, &memoryRequirements);
    const auto memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RET_CHECK(memoryTypeIndex.has_value());
    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = memoryTypeIndex.value(),
    };
    CALL_VK(vkAllocateMemory, mDevice, &allocateInfo, nullptr, &mMemory);
    vkBindImageMemory(mDevice, mImage, mMemory, 0);
    return true;
}

bool NV21Converter::createImageView() {
    const VkImageViewCreateInfo viewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = mImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components =
                    {
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                    },
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    CALL_VK(vkCreateImageView, mDevice, &viewCreateInfo, nullptr, &mImageView);
    return true;
}

void NV21Converter::createYUVConversion() {

}

void NV21Converter::createSampler() {

}

bool NV21Converter::createShaderModuleFromAsset(VkDevice device, const char* shaderFilePath,
                                 AAssetManager* assetManager, VkShaderModule* shaderModule) {
    // Read shader file from asset.
    AAsset* shaderFile = AAssetManager_open(assetManager, shaderFilePath, AASSET_MODE_BUFFER);
    RET_CHECK(shaderFile != nullptr);
    const size_t shaderSize = AAsset_getLength(shaderFile);
    std::vector<char> shader(shaderSize);
    int status = AAsset_read(shaderFile, shader.data(), shaderSize);
    AAsset_close(shaderFile);
    RET_CHECK(status >= 0);

    // Create shader module.
    const VkShaderModuleCreateInfo shaderDesc = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .flags = 0,
            .codeSize = shaderSize,
            .pCode = reinterpret_cast<const uint32_t*>(shader.data()),
    };
    CALL_VK(vkCreateShaderModule, device, &shaderDesc, nullptr, shaderModule);
    return true;
}

bool NV21Converter::bindShader(AAssetManager* assetManager) {
    VkShaderModule shaderModule;
    const char* shader = "shaders/Nv21Converter.comp.spv";
    RET_CHECK(createShaderModuleFromAsset(mDevice, shader, assetManager,
                                          &shaderModule));
    const VkPipelineLayoutCreateInfo layoutDesc = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &mDescriptorSetLayout,
            .pushConstantRangeCount =  0u,
            .pPushConstantRanges = nullptr,
    };
    CALL_VK(vkCreatePipelineLayout, mDevice, &layoutDesc, nullptr,
            &mPipelineLayout);

    const uint32_t specializationData[] = {mWorkGroupSize, mWorkGroupSize};
    const std::vector<VkSpecializationMapEntry> specializationMap = {
            // clang-format off
            // constantID, offset,               size
            {0, 0 * sizeof(uint32_t), sizeof(uint32_t)},
            {1, 1 * sizeof(uint32_t), sizeof(uint32_t)},
            // clang-format on
    };
    const VkSpecializationInfo specializationInfo = {
            .mapEntryCount = static_cast<uint32_t>(specializationMap.size()),
            .pMapEntries = specializationMap.data(),
            .dataSize = sizeof(specializationData),
            .pData = specializationData,
    };

    const VkComputePipelineCreateInfo pipelineDesc = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage =
                    {
                            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                            .module = shaderModule,
                            .pName = "main",
                            .pSpecializationInfo = &specializationInfo,
                    },
            .layout = mPipelineLayout,
    };
    CALL_VK(vkCreateComputePipelines, mDevice, VK_NULL_HANDLE, 1, &pipelineDesc, nullptr,
            &mPipeline);
    return true;

}

bool NV21Converter::copyToDeviceBuffer(void *buffer_ptr) {
    void* bufferData = nullptr;
    CALL_VK(vkMapMemory, mDevice, mMemory, 0, mInputSize, 0, &bufferData); // retrieve the host virtual address mapping to the device memory
    memcpy(bufferData, buffer_ptr, mInputSize);
    vkUnmapMemory(mDevice, mMemory); // unmap host address from device memory
    return true;
}

void NV21Converter::submitCommand() {
    const VkImageCopy imageCopy = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .srcOffset = {0, 0, 0},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffset = {0, 0, 0},
            .extent = {mWidth, mHeight, 1},
    };
//    vkCmdCopyImage(mCommandBuffer, mImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
//                   mImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
}

