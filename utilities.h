#ifndef UTILITIES_H
#define UTILITIES_H

#include <QVulkanFunctions>

static float quadVert[] = { // Y up, front = CW
    -1, -1, 0,
    -1,  1, 0,
     1, -1, 0,
     1,  1, 0
};

#define DBG Q_UNLIKELY(mWindow->isDebugEnabled())

const int MAX_INSTANCES = 16384;
const VkDeviceSize PER_INSTANCE_DATA_SIZE = 6 * sizeof(float); // instTranslate, instDiffuseAdjust

static inline VkDeviceSize aligned(VkDeviceSize v, VkDeviceSize byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

#endif // UTILITIES_H
