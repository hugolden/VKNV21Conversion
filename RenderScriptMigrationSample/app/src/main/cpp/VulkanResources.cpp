/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "VulkanResources.h"

#include <android/bitmap.h>
#include <android/hardware_buffer_jni.h>
#include <jni.h>

#include <memory>
#include <optional>
#include <vector>

#include "Utils.h"
#include "VulkanContext.h"

namespace sample {

std::unique_ptr<Buffer> Buffer::create(const VulkanContext* context, uint32_t size,
                                       VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    auto buffer = std::make_unique<Buffer>(context, size);
    const bool success = buffer->initialize(usage, properties);
    return success ? std::move(buffer) : nullptr;
}

bool Buffer::initialize(VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    // Create buffer
    const VkBufferCreateInfo bufferCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = mSize,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    CALL_VK(vkCreateBuffer, mContext->device(), &bufferCreateInfo, nullptr, mBuffer.pHandle());

    // Allocate memory for the buffer
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(mContext->device(), mBuffer.handle(), &memoryRequirements);
    const auto memoryTypeIndex =
            mContext->findMemoryType(memoryRequirements.memoryTypeBits, properties);
    RET_CHECK(memoryTypeIndex.has_value());
    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = memoryTypeIndex.value(),
    };
    CALL_VK(vkAllocateMemory, mContext->device(), &allocateInfo, nullptr, mMemory.pHandle());

    vkBindBufferMemory(mContext->device(), mBuffer.handle(), mMemory.handle(), 0);
    return true;
}

bool Buffer::copyFrom(const void* data) {
    void* bufferData = nullptr;
    CALL_VK(vkMapMemory, mContext->device(), mMemory.handle(), 0, mSize, 0, &bufferData);
    memcpy(bufferData, data, mSize);
    vkUnmapMemory(mContext->device(), mMemory.handle());
    return true;
}

std::unique_ptr<Image> Image::createDeviceLocal(const VulkanContext* context, uint32_t width,
                                                uint32_t height, VkImageUsageFlags usage) {
    auto image = std::make_unique<Image>(context, width, height);
    bool success = image->createDeviceLocalImage(usage) && image->createImageView();
    // Sampler is only needed for sampled images.
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        success = success && image->createSampler();
    }
    return success ? std::move(image) : nullptr;
}

void assign_sampler(VkSampler* origin, VkSampler* assignee ){
    *origin = *assignee;
}

std::unique_ptr<Image> Image::createYUVDeviceLocal(const VulkanContext *context, VkSampler sampler,VkSamplerYcbcrConversionInfo conversion,
                                                   uint32_t width, uint32_t height, VkImageUsageFlags usage) {
    auto image = std::make_unique<Image>(context, width, height);
    assign_sampler(image->mSampler.pHandle(),&sampler);
    image->mYuvConversionInfo = conversion;
    bool success = image->createDeviceLocalNV21Image(usage) && image->createNV21ImageView();
    // Sampler is only needed for sampled images.
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        success = success && image->createSampler();
    }
    return success ? std::move(image) : nullptr;
}



std::unique_ptr<Image> Image::createFromBitmap(const VulkanContext* context, JNIEnv* env,
                                               jobject bitmap) {
    // Get bitmap info
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("Image::createFromBitmap: Failed to AndroidBitmap_getInfo");
        return nullptr;
    }

    // Create device local image
    auto image =
            Image::createDeviceLocal(context, info.width, info.height,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if (image == nullptr) return nullptr;

    // Set content from bitmap
    const bool success = image->setContentFromBitmap(env, bitmap);
    return success ? std::move(image) : nullptr;
}

std::unique_ptr<Image> Image::createFromNV21Buffer(const VulkanContext* context, VkSampler sampler, VkSamplerYcbcrConversionInfo conversion,uint32_t width, uint32_t height){
    auto image =
            Image::createYUVDeviceLocal(context,sampler, conversion, width, height,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    const bool success = image != nullptr;
    return success ? std::move(image) : nullptr;
}

bool Image::feedImageViewWithBuffer(void *nv21ptr, uint32_t width, uint32_t height) {
    const bool success = setContentFromNV21Ptr(nv21ptr,width,height);
    return success;
}

std::unique_ptr<Image> Image::createFromAHardwareBuffer(const VulkanContext* context,
                                                        AHardwareBuffer* buffer) {
    auto image = std::make_unique<Image>(context);
    const bool success = image->createImageFromAHardwareBuffer(buffer);
    return success ? std::move(image) : nullptr;
}


bool Image::createDeviceLocalImage(VkImageUsageFlags usage) {
    // Create an image
    const VkImageCreateInfo imageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {mWidth, mHeight, 1},
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
    CALL_VK(vkCreateImage, mContext->device(), &imageCreateInfo, nullptr, mImage.pHandle());

    // Allocate device memory
    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(mContext->device(), mImage.handle(), &memoryRequirements);
    const auto memoryTypeIndex = mContext->findMemoryType(memoryRequirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RET_CHECK(memoryTypeIndex.has_value());
    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = memoryTypeIndex.value(),
    };
    CALL_VK(vkAllocateMemory, mContext->device(), &allocateInfo, nullptr, mMemory.pHandle());
    vkBindImageMemory(mContext->device(), mImage.handle(), mMemory.handle(), 0);
    return true;
}

bool Image::createDeviceLocalNV21Image(VkImageUsageFlags usage) {
    const VkImageCreateInfo imageCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = VK_IMAGE_CREATE_DISJOINT_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
            .extent = {mWidth, mHeight, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    CALL_VK(vkCreateImage, mContext->device(), &imageCreateInfo, nullptr, mImage.pHandle());

    // Allocate device memory
    VkImagePlaneMemoryRequirementsInfo image_plane_info = {};
    image_plane_info.sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
    image_plane_info.pNext = nullptr;
    image_plane_info.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
    VkImageMemoryRequirementsInfo2 image_info2 = {};
    image_info2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    image_info2.pNext = &image_plane_info;
    image_info2.image = mImage.handle();

    VkImagePlaneMemoryRequirementsInfo memory_plane_requirements = {};
    memory_plane_requirements.sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
    memory_plane_requirements.pNext = nullptr;
    memory_plane_requirements.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;

    VkMemoryRequirements2 memory_requirements2 = {};
    memory_requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memory_requirements2.pNext = &memory_plane_requirements;
    vkGetImageMemoryRequirements2(mContext->device(), &image_info2, &memory_requirements2);
    VkDeviceSize image_size = memory_requirements2.memoryRequirements.size;
    uint32_t image_bits = memory_requirements2.memoryRequirements.memoryTypeBits;

    VkDeviceSize memory_offset_plane0, memory_offset_plane1;
    //Set offsets
    memory_offset_plane0 = 0;
    memory_offset_plane1 = image_size;
//Plane 1
    image_plane_info.planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
    memory_plane_requirements.planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
    vkGetImageMemoryRequirements2(mContext->device(), &image_info2, &memory_requirements2);
    image_size += memory_requirements2.memoryRequirements.size;
    image_bits = image_bits | memory_requirements2.memoryRequirements.memoryTypeBits;


    const auto memoryTypeIndex = mContext->findMemoryType(image_bits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RET_CHECK(memoryTypeIndex.has_value());
    const VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = image_size,
            .memoryTypeIndex = memoryTypeIndex.value(),
    };
    CALL_VK(vkAllocateMemory, mContext->device(), &allocateInfo, nullptr, mMemory.pHandle());

    std::vector<VkBindImageMemoryInfo> bind_image_memory_infos(2);

    VkBindImagePlaneMemoryInfo bind_image_plane0_info = {};
    bind_image_plane0_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
    bind_image_plane0_info.pNext = nullptr;
    bind_image_plane0_info.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;
    VkBindImageMemoryInfo& bind_image_memory_plane0_info = bind_image_memory_infos[0];
    bind_image_memory_plane0_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bind_image_memory_plane0_info.pNext = &bind_image_plane0_info;
    bind_image_memory_plane0_info.image = mImage.handle();
    bind_image_memory_plane0_info.memory = mMemory.handle();
    bind_image_memory_plane0_info.memoryOffset = memory_offset_plane0;
//Plane 1
    VkBindImagePlaneMemoryInfo bind_image_plane1_info = {};
    bind_image_plane1_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
    bind_image_plane1_info.pNext = nullptr;
    bind_image_plane1_info.planeAspect = VK_IMAGE_ASPECT_PLANE_1_BIT;
    VkBindImageMemoryInfo& bind_image_memory_plane1_info = bind_image_memory_infos[1];
    bind_image_memory_plane1_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bind_image_memory_plane1_info.pNext = &bind_image_plane1_info;
    bind_image_memory_plane1_info.image = mImage.handle();
    bind_image_memory_plane1_info.memory = mMemory.handle();
    bind_image_memory_plane1_info.memoryOffset = memory_offset_plane1;

    vkBindImageMemory2(mContext->device(), static_cast<uint32_t>(bind_image_memory_infos.size()), bind_image_memory_infos.data());
    return true;
}


bool Image::setContentFromBitmap(JNIEnv* env, jobject bitmap) {
    // Get bitmap info
    AndroidBitmapInfo info;
    RET_CHECK(AndroidBitmap_getInfo(env, bitmap, &info) == ANDROID_BITMAP_RESULT_SUCCESS);
    RET_CHECK(info.width == mWidth);
    RET_CHECK(info.height == mHeight);
    RET_CHECK(info.format == ANDROID_BITMAP_FORMAT_RGBA_8888);
    RET_CHECK(info.stride % 4 == 0);

    // Allocate a staging buffer
    const uint32_t bufferSize = info.stride * info.height;
    auto stagingBuffer = Buffer::create(
            mContext, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy bitmap pixels to the buffer memory
    void* bitmapData = nullptr;
    RET_CHECK(AndroidBitmap_lockPixels(env, bitmap, &bitmapData) == ANDROID_BITMAP_RESULT_SUCCESS);
    const bool success = stagingBuffer->copyFrom(bitmapData);
    AndroidBitmap_unlockPixels(env, bitmap);
    RET_CHECK(success);

    // Set layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL to prepare for buffer-image copy
    RET_CHECK(transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

    // Copy buffer to image
    VulkanCommandBuffer copyCommand(mContext->device(), mContext->commandPool());
    RET_CHECK(mContext->beginSingleTimeCommand(copyCommand.pHandle()));
    const VkBufferImageCopy bufferImageCopy = {
            .bufferOffset = 0,
            .bufferRowLength = info.stride / 4,
            .bufferImageHeight = mHeight,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {mWidth, mHeight, 1},
    };
    vkCmdCopyBufferToImage(copyCommand.handle(), stagingBuffer->getBufferHandle(), mImage.handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
    RET_CHECK(mContext->endAndSubmitSingleTimeCommand(copyCommand.handle()));

    // Set layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL to prepare for input sampler usage
    RET_CHECK(transitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    return true;
}

bool Image::setContentFromNV21Ptr(void *nv21ptr, uint32_t width, uint32_t height) {
    if(width != mWidth || height!=mHeight){
        return false;
    }
    const uint32_t bufferSize = static_cast<const uint32_t>(width * height * 1.5);
    auto stagingBuffer = Buffer::create(
            mContext, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy bitmap pixels to the buffer memory
    RET_CHECK(stagingBuffer->copyFrom(nv21ptr));

    // Set layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL to prepare for buffer-image copy
    RET_CHECK(transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));

    // Copy buffer to image
    VulkanCommandBuffer copyCommand(mContext->device(), mContext->commandPool());
    RET_CHECK(mContext->beginSingleTimeCommand(copyCommand.pHandle()));

    std::vector<VkBufferImageCopy> plane_regions(2);
    plane_regions[0].bufferOffset = 0;
    plane_regions[0].bufferRowLength = 0;
    plane_regions[0].bufferImageHeight = 0;
    plane_regions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
    plane_regions[0].imageSubresource.mipLevel = 0;
    plane_regions[0].imageSubresource.baseArrayLayer = 0;
    plane_regions[0].imageSubresource.layerCount = 1;
    plane_regions[0].imageOffset = { 0, 0, 0 };
    plane_regions[0].imageExtent = { width, height, 1 };
    plane_regions[1].bufferOffset = width*height;
    plane_regions[1].bufferRowLength = 0;
    plane_regions[1].bufferImageHeight = 0;
    plane_regions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
    plane_regions[1].imageSubresource.mipLevel = 0;
    plane_regions[1].imageSubresource.baseArrayLayer = 0;
    plane_regions[1].imageSubresource.layerCount = 1;
    plane_regions[1].imageOffset = { 0, 0, 0 };
    plane_regions[1].imageExtent = { width / 2, height / 2, 1 };

    vkCmdCopyBufferToImage(copyCommand.handle(), stagingBuffer->getBufferHandle(), mImage.handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(plane_regions.size()), plane_regions.data());
    RET_CHECK(mContext->endAndSubmitSingleTimeCommand(copyCommand.handle()));

    // Set layout to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL to prepare for input sampler usage
    RET_CHECK(transitionLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    return true;
}

bool Image::createImageFromAHardwareBuffer(AHardwareBuffer* buffer) {
    // Acquire the AHardwareBuffer and get the descriptor
    AHardwareBuffer_acquire(buffer);
    AHardwareBuffer_Desc ahwbDesc{};
    AHardwareBuffer_describe(buffer, &ahwbDesc);
    mBuffer = buffer;
    mWidth = ahwbDesc.width;
    mHeight = ahwbDesc.height;

    // Get AHardwareBuffer properties
    VkAndroidHardwareBufferFormatPropertiesANDROID formatInfo = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
            .pNext = nullptr,
    };
    VkAndroidHardwareBufferPropertiesANDROID properties = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
            .pNext = &formatInfo,
    };
    CALL_VK(vkGetAndroidHardwareBufferPropertiesANDROID, mContext->device(), mBuffer, &properties);

    // Create an image to bind to our AHardwareBuffer
    VkExternalMemoryImageCreateInfo externalCreateInfo{
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
    };
    VkImageCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &externalCreateInfo,
            .flags = 0u,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {ahwbDesc.width, ahwbDesc.height, 1u},
            .mipLevels = 1u,
            .arrayLayers = 1u,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CALL_VK(vkCreateImage, mContext->device(), &createInfo, nullptr, mImage.pHandle());

    // Allocate device memory
    const auto memoryTypeIndex = mContext->findMemoryType(properties.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    RET_CHECK(memoryTypeIndex.has_value());
    VkImportAndroidHardwareBufferInfoANDROID androidHardwareBufferInfo{
            .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
            .pNext = nullptr,
            .buffer = mBuffer,
    };
    VkMemoryDedicatedAllocateInfo memoryAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext = &androidHardwareBufferInfo,
            .image = mImage.handle(),
            .buffer = VK_NULL_HANDLE,
    };
    VkMemoryAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &memoryAllocateInfo,
            .allocationSize = properties.allocationSize,
            .memoryTypeIndex = memoryTypeIndex.value(),
    };
    CALL_VK(vkAllocateMemory, mContext->device(), &allocateInfo, nullptr, mMemory.pHandle());

    // Bind image to the device memory
    CALL_VK(vkBindImageMemory, mContext->device(), mImage.handle(), mMemory.handle(), 0);
    RET_CHECK(transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
    return true;
}

bool Image::createSampler() {
    const VkSamplerCreateInfo samplerCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            // Use clamp to edge for BLUR filter
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_NEVER,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            // Use unnormalized coordinates to avoid the need of normalization
            // when indexing into the texture
            .unnormalizedCoordinates = VK_TRUE,
    };
    CALL_VK(vkCreateSampler, mContext->device(), &samplerCreateInfo, nullptr, mSampler.pHandle());
    return true;
}


bool Image::createImageView() {
    const VkImageViewCreateInfo viewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = mImage.handle(),
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
    CALL_VK(vkCreateImageView, mContext->device(), &viewCreateInfo, nullptr, mImageView.pHandle());
    return true;
}

bool Image::createNV21ImageView() {
    const VkImageViewCreateInfo viewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = &mYuvConversionInfo,
            .flags = 0,
            .image = mImage.handle(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM_KHR,
            .components =
                    {
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                    },
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    CALL_VK(vkCreateImageView, mContext->device(), &viewCreateInfo, nullptr, mImageView.pHandle());
    return true;
}

void Image::recordLayoutTransitionBarrier(VkCommandBuffer cmd, VkImageLayout newLayout,
                                          bool preserveData) {
    if (newLayout == mLayout) return;
    if (!preserveData) mLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const auto getAccessMask = [](VkImageLayout layout) -> VkAccessFlags {
        switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                return 0;
            case VK_IMAGE_LAYOUT_GENERAL:
                // In this sample app, we only use GENERAL layout for output storage images.
                return VK_ACCESS_SHADER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return VK_ACCESS_TRANSFER_READ_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_ACCESS_TRANSFER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_ACCESS_SHADER_READ_BIT;
            default:
                LOGE("Undefined access mask for target image layout");
                return 0;
        }
    };
    const auto getStageFlag = [](VkImageLayout layout) -> VkPipelineStageFlags {
        switch (layout) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            case VK_IMAGE_LAYOUT_GENERAL:
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_PIPELINE_STAGE_TRANSFER_BIT;
            default:
                LOGE("Undefined stage flag for target image layout");
                return 0;
        }
    };

    const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = getAccessMask(mLayout),
            .dstAccessMask = getAccessMask(newLayout),
            .oldLayout = mLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = mImage.handle(),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, getStageFlag(mLayout), getStageFlag(newLayout), 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    mLayout = newLayout;
}

bool Image::transitionLayout(VkImageLayout newLayout) {
    if (newLayout == mLayout) return true;
    VulkanCommandBuffer layoutCommand(mContext->device(), mContext->commandPool());
    RET_CHECK(mContext->beginSingleTimeCommand(layoutCommand.pHandle()));
    recordLayoutTransitionBarrier(layoutCommand.handle(), newLayout);
    RET_CHECK(mContext->endAndSubmitSingleTimeCommand(layoutCommand.handle()));
    return true;
}


}  // namespace sample
