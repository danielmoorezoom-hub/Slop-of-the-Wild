// What AMD's FSR takes in (fsr_api.h): this frame's scene, depth, motion vectors (vulkan/mvec.c) and reactive mask at the render size,
// the scene out.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

typedef struct {
    VkImage image;
    VkFormat format;
    uint32_t width, height;
} Fsr3Image;

typedef struct {
    VkDevice dev;
    VkPhysicalDevice phys;
    VkCommandBuffer cb;
    // inputs at the render size (every image in VK_IMAGE_LAYOUT_GENERAL, as they are left): the scene (LDR), its depth
    // (R32F, [0, 1], not inverted), the motion vectors (.xy: see motion_scale) and the reactive mask (R8); the output (storage)
    Fsr3Image color, depth, motion, reactive, output;
    float jitter[2];        // the camera's jitter this frame, render pixels (FSR's convention: x right, y down)
    float motion_scale[2];  // .xy times this (over the render size) is the UV offset to where the pixel was
    float dt_ms;            // time since the last frame
    float near_, far_, fov_y;  // the camera (fov_y: radians)
    float sharpness;        // RCAS, 0..1 (0: off)
    bool reset;             // no usable history (a cut, the first frame)
} Fsr3Dispatch;
