//
// Created by Hu Sichao on 4/30/2022.
//

#ifndef RENDERSCRIPT_MIGRATION_SAMPLE_NV21CONVERTER_H
#define RENDERSCRIPT_MIGRATION_SAMPLE_NV21CONVERTER_H

#endif //RENDERSCRIPT_MIGRATION_SAMPLE_NV21CONVERTER_H
#include <android/hardware_buffer_jni.h>

#include <memory>
#include <optional>
#include <vector>
#include <android/asset_manager.h>

#include "Utils.h"

class NV21Converter{
public:
    NV21Converter();
    virtual ~NV21Converter();

    bool initialize(AAssetManager* assetManager);

    void configureFrameSize(uint32_t width, uint32_t height);

    void feedBuffer(void* buffer_ptr);

    void destroy();

private:
    uint32_t mInstanceVersion = 0;

    uint32_t mWidth,mHeight;

    uint32_t mInputSize;

    uint32_t mOutputSize;

    VkInstance mInstance;

    AAssetManager* mAssetManager;

    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;

    uint32_t mQueueFamilyIndex = 0;
    uint32_t mWorkGroupSize = 0;

    VkPhysicalDeviceProperties mPhysicalDeviceProperties;
    VkPhysicalDeviceMemoryProperties mPhysicalDeviceMemoryProperties;

    VkDescriptorSetLayout mDescriptorSetLayout;

    VkDevice mDevice;

    VkQueue mQueue = VK_NULL_HANDLE;

    VkDescriptorPool mDescriptorPool;

    VkCommandPool mCommandPool;

    VkCommandBuffer mCommandBuffer;

    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;

    VkImage mImage;

    VkImageView mImageView;

    VkPipelineLayout  mPipelineLayout;

    VkPipeline mPipeline;

    std::optional<uint32_t> findMemoryType(uint32_t memoryTypeBits,
                                                         VkFlags properties);

    VkDeviceMemory mMemory;

    bool initDevice(); //

    bool bindUniformLayout();

    bool createPools();

    uint32_t chooseWorkGroupSize(const VkPhysicalDeviceLimits& limits);

    bool initCommandBuffer();

    bool allocateDeviceMemory(uint32_t width,uint32_t height);

    bool createImageView();

    void createYUVConversion();

    void createSampler();

    bool bindShader(AAssetManager* assetManager);

    bool createShaderModuleFromAsset(VkDevice device, const char* shaderFilePath,
                                     AAssetManager* assetManager, VkShaderModule* shaderModule);

    bool copyToDeviceBuffer(void* buffer_ptr);

    void submitCommand();



};
