#ifndef WINE_NX_DXVK_REQUIREMENTS_H
#define WINE_NX_DXVK_REQUIREMENTS_H

/* DXVK 3.1.1 getFeatureList(), plus the FL11_0 checks below. */
#define NX_DXVK_FEATURES_10(X) \
    X(depthBiasClamp) X(depthClamp) X(dualSrcBlend) X(fillModeNonSolid) \
    X(fragmentStoresAndAtomics) X(fullDrawIndexUint32) X(geometryShader) \
    X(imageCubeArray) X(independentBlend) X(multiDrawIndirect) X(multiViewport) \
    X(occlusionQueryPrecise) X(robustBufferAccess) X(sampleRateShading) \
    X(samplerAnisotropy) X(shaderClipDistance) X(shaderCullDistance) \
    X(shaderImageGatherExtended) X(shaderInt16) X(shaderInt64) \
    X(shaderSampledImageArrayDynamicIndexing) X(textureCompressionBC)

#define NX_DXVK_FEATURES_11(X) X(shaderDrawParameters) X(storageBuffer16BitAccess)

#define NX_DXVK_FEATURES_12(X) \
    X(bufferDeviceAddress) X(descriptorIndexing) X(storageBuffer8BitAccess) \
    X(descriptorBindingSampledImageUpdateAfterBind) X(descriptorBindingUpdateUnusedWhilePending) \
    X(descriptorBindingPartiallyBound) X(hostQueryReset) X(runtimeDescriptorArray) \
    X(samplerMirrorClampToEdge) X(scalarBlockLayout) X(shaderInt8) X(timelineSemaphore) \
    X(uniformBufferStandardLayout) X(vulkanMemoryModel)

#define NX_DXVK_FEATURES_13(X) \
    X(inlineUniformBlock) X(computeFullSubgroups) X(dynamicRendering) X(maintenance4) \
    X(shaderDemoteToHelperInvocation) X(shaderZeroInitializeWorkgroupMemory) \
    X(subgroupSizeControl) X(synchronization2)

#define NX_DXVK_FL11_FEATURES(X) X(drawIndirectFirstInstance) X(tessellationShader)

#endif
