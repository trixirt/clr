/* Copyright (c) 2015 - 2021 Advanced Micro Devices, Inc.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#include <hip/hip_runtime.h>
#include <elf/elf.hpp>
#include <fstream>

#include "hip_internal.hpp"
#include "platform/program.hpp"
#include "hip_event.hpp"
#include "hip_platform.hpp"

hipError_t ihipModuleLoadData(hipModule_t* module, const void* mmap_ptr, size_t mmap_size);

extern hipError_t ihipLaunchKernel(const void* hostFunction, dim3 gridDim, dim3 blockDim,
                                   void** args, size_t sharedMemBytes, hipStream_t stream,
                                   hipEvent_t startEvent, hipEvent_t stopEvent, int flags);

const std::string& FunctionName(const hipFunction_t f) {
  return hip::DeviceFunc::asFunction(f)->kernel()->name();
}

static uint64_t ElfSize(const void* emi) { return amd::Elf::getElfSize(emi); }

hipError_t hipModuleUnload(hipModule_t hmod) {
  HIP_INIT_API(hipModuleUnload, hmod);
  if (hmod == nullptr) {
    HIP_RETURN(hipErrorInvalidResourceHandle);
  }
  HIP_RETURN(PlatformState::instance().unloadModule(hmod));
}

hipError_t hipModuleLoad(hipModule_t* module, const char* fname) {
  HIP_INIT_API(hipModuleLoad, module, fname);

  HIP_RETURN(PlatformState::instance().loadModule(module, fname));
}

hipError_t hipModuleLoadData(hipModule_t* module, const void* image) {
  HIP_INIT_API(hipModuleLoadData, module, image);
  HIP_RETURN(PlatformState::instance().loadModule(module, 0, image));
}

hipError_t hipModuleLoadDataEx(hipModule_t* module, const void* image, unsigned int numOptions,
                               hipJitOption* options, void** optionsValues) {
  /* TODO: Pass options to Program */
  HIP_INIT_API(hipModuleLoadDataEx, module, image);
  HIP_RETURN(PlatformState::instance().loadModule(module, 0, image));
}

extern hipError_t __hipExtractCodeObjectFromFatBinary(
    const void* data, const std::vector<std::string>& devices,
    std::vector<std::pair<const void*, size_t>>& code_objs);

hipError_t hipModuleGetFunction(hipFunction_t* hfunc, hipModule_t hmod, const char* name) {
  HIP_INIT_API(hipModuleGetFunction, hfunc, hmod, name);

  if (hfunc == nullptr || name == nullptr || strlen(name) == 0) {
    HIP_RETURN(hipErrorInvalidValue);
  }
  if (hmod == nullptr) {
    HIP_RETURN(hipErrorInvalidResourceHandle);
  }

  if (hipSuccess != PlatformState::instance().getDynFunc(hfunc, hmod, name)) {
    LogPrintfError("Cannot find the function: %s for module: 0x%x \n", name, hmod);
    HIP_RETURN(hipErrorNotFound);
  }

  HIP_RETURN(hipSuccess);
}

hipError_t hipModuleGetGlobal(hipDeviceptr_t* dptr, size_t* bytes, hipModule_t hmod,
                              const char* name) {
  HIP_INIT_API(hipModuleGetGlobal, dptr, bytes, hmod, name);

  if (dptr == nullptr || bytes == nullptr) {
    // If either is nullptr, ignore it
    HIP_RETURN(hipSuccess);
  }
  if ((dptr == nullptr && bytes == nullptr) || name == nullptr || strlen(name) == 0) {
    HIP_RETURN(hipErrorInvalidValue);
  }
  if (hmod == nullptr) {
    HIP_RETURN(hipErrorInvalidResourceHandle);
  }
  /* Get address and size for the global symbol */
  if (hipSuccess != PlatformState::instance().getDynGlobalVar(name, hmod, dptr, bytes)) {
    LogPrintfError("Cannot find global Var: %s for module: 0x%x at device: %d \n", name, hmod,
                   ihipGetDevice());
    HIP_RETURN(hipErrorNotFound);
  }

  HIP_RETURN(hipSuccess);
}

hipError_t hipFuncGetAttribute(int* value, hipFunction_attribute attrib, hipFunction_t hfunc) {
  HIP_INIT_API(hipFuncGetAttribute, value, attrib, hfunc);

  if ((value == nullptr)) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  hip::DeviceFunc* function = hip::DeviceFunc::asFunction(hfunc);
  if (function == nullptr) {
    HIP_RETURN(hipErrorInvalidHandle);
  }

  amd::Kernel* kernel = function->kernel();
  if (kernel == nullptr) {
    HIP_RETURN(hipErrorInvalidDeviceFunction);
  }

  const device::Kernel::WorkGroupInfo* wrkGrpInfo =
      kernel->getDeviceKernel(*(hip::getCurrentDevice()->devices()[0]))->workGroupInfo();
  if (wrkGrpInfo == nullptr) {
    HIP_RETURN(hipErrorMissingConfiguration);
  }

  switch (attrib) {
    case HIP_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES:
      *value = static_cast<int>(wrkGrpInfo->localMemSize_);
      break;
    case HIP_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK:
      *value = static_cast<int>(wrkGrpInfo->size_);
      break;
    case HIP_FUNC_ATTRIBUTE_CONST_SIZE_BYTES:
      *value = 0;
      break;
    case HIP_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES:
      *value = static_cast<int>(wrkGrpInfo->privateMemSize_);
      break;
    case HIP_FUNC_ATTRIBUTE_NUM_REGS:
      *value = static_cast<int>(wrkGrpInfo->usedVGPRs_);
      break;
    case HIP_FUNC_ATTRIBUTE_PTX_VERSION:
      *value = 30;  // Defaults to 3.0 as HCC
      break;
    case HIP_FUNC_ATTRIBUTE_BINARY_VERSION:
      *value = static_cast<int>(kernel->signature().version());
      break;
    case HIP_FUNC_ATTRIBUTE_CACHE_MODE_CA:
      *value = 0;
      break;
    case HIP_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES:
      *value = static_cast<int>(wrkGrpInfo->availableLDSSize_ - wrkGrpInfo->localMemSize_);
      break;
    case HIP_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT:
      *value = 0;
      break;
    default:
      HIP_RETURN(hipErrorInvalidValue);
  }

  HIP_RETURN(hipSuccess);
}

hipError_t hipFuncGetAttributes(hipFuncAttributes* attr, const void* func) {
  HIP_INIT_API(hipFuncGetAttributes, attr, func);

  HIP_RETURN_ONFAIL(PlatformState::instance().getStatFuncAttr(attr, func, ihipGetDevice()));

  HIP_RETURN(hipSuccess);
}

hipError_t hipFuncSetAttribute(const void* func, hipFuncAttribute attr, int value) {
  HIP_INIT_API(hipFuncSetAttribute, func, attr, value);

  // No way to set function attribute yet.

  HIP_RETURN(hipSuccess);
}

hipError_t hipFuncSetCacheConfig(const void* func, hipFuncCache_t cacheConfig) {
  HIP_INIT_API(hipFuncSetCacheConfig, cacheConfig);

  // No way to set cache config yet.

  HIP_RETURN(hipSuccess);
}

hipError_t hipFuncSetSharedMemConfig(const void* func, hipSharedMemConfig config) {
  HIP_INIT_API(hipFuncSetSharedMemConfig, func, config);

  // No way to set Shared Memory config function yet.

  HIP_RETURN(hipSuccess);
}

hipError_t ihipLaunchKernel_validate(hipFunction_t f, uint32_t globalWorkSizeX,
                                     uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ,
                                     uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                                     uint32_t sharedMemBytes, void** kernelParams, void** extra,
                                     int deviceId, uint32_t params = 0) {
  if (f == nullptr) {
    LogPrintfError("%s", "Function passed is null");
    return hipErrorInvalidImage;
  }
  if ((kernelParams != nullptr) && (extra != nullptr)) {
    LogPrintfError("%s",
                   "Both, kernelParams and extra Params are provided, only one should be provided");
    return hipErrorInvalidValue;
  }
  if (globalWorkSizeX == 0 || globalWorkSizeY == 0 || globalWorkSizeZ == 0 || blockDimX == 0 ||
      blockDimY == 0 || blockDimZ == 0) {
    return hipErrorInvalidValue;
  }

  const amd::Device* device = g_devices[deviceId]->devices()[0];
  const auto& info = device->info();
  if (sharedMemBytes > info.localMemSizePerCU_) { //sharedMemPerBlock
    return hipErrorInvalidValue;
  }
  // Make sure dispatch doesn't exceed max workgroup size limit
  if (blockDimX * blockDimY * blockDimZ > info.maxWorkGroupSize_) {
    return hipErrorInvalidValue;
  }
  hip::DeviceFunc* function = hip::DeviceFunc::asFunction(f);
  amd::Kernel* kernel = function->kernel();
  if (!kernel->getDeviceKernel(*device)) {
    return hipErrorInvalidDevice;
  }
  // Make sure the launch params are not larger than if specified launch_bounds
  // If it exceeds, then return a failure
  if (blockDimX * blockDimY * blockDimZ >
      kernel->getDeviceKernel(*device)->workGroupInfo()->size_) {
    LogPrintfError("Launch params (%u, %u, %u) are larger than launch bounds (%lu) for kernel %s",
                   blockDimX, blockDimY, blockDimZ,
                   kernel->getDeviceKernel(*device)->workGroupInfo()->size_,
                   function->name().c_str());
    return hipErrorLaunchFailure;
  }

  if (params & amd::NDRangeKernelCommand::CooperativeGroups) {
    if (!device->info().cooperativeGroups_) {
      return hipErrorLaunchFailure;
    }
    int num_blocks = 0;
    int max_blocks_per_grid = 0;
    int best_block_size = 0;
    int block_size = blockDimX * blockDimY * blockDimZ;
    hipError_t err = hip_impl::ihipOccupancyMaxActiveBlocksPerMultiprocessor(
        &num_blocks, &max_blocks_per_grid, &best_block_size, *device, f, block_size, sharedMemBytes,
        true);
    if (err != hipSuccess) {
      return err;
    }
    if (((globalWorkSizeX * globalWorkSizeY * globalWorkSizeZ) / block_size) >
        unsigned(max_blocks_per_grid)) {
      return hipErrorCooperativeLaunchTooLarge;
    }
  }
  if (params & amd::NDRangeKernelCommand::CooperativeMultiDeviceGroups) {
    if (!device->info().cooperativeMultiDeviceGroups_) {
      return hipErrorLaunchFailure;
    }
  }
  address kernargs = nullptr;
  // 'extra' is a struct that contains the following info: {
  //   HIP_LAUNCH_PARAM_BUFFER_POINTER, kernargs,
  //   HIP_LAUNCH_PARAM_BUFFER_SIZE, &kernargs_size,
  //   HIP_LAUNCH_PARAM_END }
  if (extra != nullptr) {
    if (extra[0] != HIP_LAUNCH_PARAM_BUFFER_POINTER || extra[2] != HIP_LAUNCH_PARAM_BUFFER_SIZE ||
        extra[4] != HIP_LAUNCH_PARAM_END) {
      return hipErrorInvalidValue;
    }
    kernargs = reinterpret_cast<address>(extra[1]);
  }

  const amd::KernelSignature& signature = kernel->signature();
  for (size_t i = 0; i < signature.numParameters(); ++i) {
    const amd::KernelParameterDescriptor& desc = signature.at(i);
    if (kernelParams == nullptr) {
      assert(kernargs != nullptr);
      kernel->parameters().set(i, desc.size_, kernargs + desc.offset_,
                               desc.type_ == T_POINTER /*svmBound*/);
    } else {
      assert(extra == nullptr);
      kernel->parameters().set(i, desc.size_, kernelParams[i],
                               desc.type_ == T_POINTER /*svmBound*/);
    }
  }
  return hipSuccess;
}

hipError_t ihipLaunchKernelCommand(amd::Command*& command, hipFunction_t f,
                                   uint32_t globalWorkSizeX, uint32_t globalWorkSizeY,
                                   uint32_t globalWorkSizeZ, uint32_t blockDimX, uint32_t blockDimY,
                                   uint32_t blockDimZ, uint32_t sharedMemBytes,
                                   hip::Stream* stream, void** kernelParams, void** extra,
                                   hipEvent_t startEvent = nullptr, hipEvent_t stopEvent = nullptr,
                                   uint32_t flags = 0, uint32_t params = 0, uint32_t gridId = 0,
                                   uint32_t numGrids = 0, uint64_t prevGridSum = 0,
                                   uint64_t allGridSum = 0, uint32_t firstDevice = 0) {
  hip::DeviceFunc* function = hip::DeviceFunc::asFunction(f);
  amd::Kernel* kernel = function->kernel();

  size_t globalWorkOffset[3] = {0};
  size_t globalWorkSize[3] = {globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ};
  size_t localWorkSize[3] = {blockDimX, blockDimY, blockDimZ};
  amd::NDRangeContainer ndrange(3, globalWorkOffset, globalWorkSize, localWorkSize);
  amd::Command::EventWaitList waitList;
  address kernargs = nullptr;

  bool profileNDRange = (startEvent != nullptr || stopEvent != nullptr);

  // Flag set to 1 signifies that kernel can be launched in anyorder
  if (flags & hipExtAnyOrderLaunch) {
    params |= amd::NDRangeKernelCommand::AnyOrderLaunch;
  }

  amd::NDRangeKernelCommand* kernelCommand = new amd::NDRangeKernelCommand(
      *stream, waitList, *kernel, ndrange, sharedMemBytes, params, gridId, numGrids, prevGridSum,
      allGridSum, firstDevice, profileNDRange);
  if (!kernelCommand) {
    return hipErrorOutOfMemory;
  }

  // Capture the kernel arguments
  if (CL_SUCCESS != kernelCommand->captureAndValidate()) {
    kernelCommand->release();
    return hipErrorOutOfMemory;
  }

  command = kernelCommand;

  return hipSuccess;
}

hipError_t ihipModuleLaunchKernel(hipFunction_t f, uint32_t globalWorkSizeX,
                                  uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ,
                                  uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                                  uint32_t sharedMemBytes, hipStream_t hStream, void** kernelParams,
                                  void** extra, hipEvent_t startEvent, hipEvent_t stopEvent,
                                  uint32_t flags = 0, uint32_t params = 0, uint32_t gridId = 0,
                                  uint32_t numGrids = 0, uint64_t prevGridSum = 0,
                                  uint64_t allGridSum = 0, uint32_t firstDevice = 0) {
  int deviceId = hip::Stream::DeviceId(hStream);
  HIP_RETURN_ONFAIL(PlatformState::instance().initStatManagedVarDevicePtr(deviceId));

  if (f == nullptr) {
    LogPrintfError("%s", "Function passed is null");
    return hipErrorInvalidResourceHandle;
  }
  hip::DeviceFunc* function = hip::DeviceFunc::asFunction(f);
  amd::Kernel* kernel = function->kernel();
  amd::ScopedLock lock(function->dflock_);

  hipError_t status = ihipLaunchKernel_validate(
      f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ, blockDimX, blockDimY, blockDimZ,
      sharedMemBytes, kernelParams, extra, deviceId, params);
  if (status != hipSuccess) {
    return status;
  }
  // Make sure the app doesn't launch a workgroup bigger than the global size
  if (globalWorkSizeX < blockDimX) blockDimX = globalWorkSizeX;
  if (globalWorkSizeY < blockDimY) blockDimY = globalWorkSizeY;
  if (globalWorkSizeZ < blockDimZ) blockDimZ = globalWorkSizeZ;

  auto device = g_devices[deviceId]->devices()[0];
  // Check if it's a uniform kernel and validate dimensions
  if (kernel->getDeviceKernel(*device)->getUniformWorkGroupSize()) {
    if (((globalWorkSizeX % blockDimX) != 0) ||
        ((globalWorkSizeY % blockDimY) != 0) ||
        ((globalWorkSizeZ % blockDimZ) != 0)) {
      return hipErrorInvalidValue;
    }
  }
  amd::Command* command = nullptr;
  hip::Stream* hip_stream = hip::getStream(hStream);
  status = ihipLaunchKernelCommand(command, f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ,
                                   blockDimX, blockDimY, blockDimZ, sharedMemBytes, hip_stream,
                                   kernelParams, extra, startEvent, stopEvent, flags, params,
                                   gridId, numGrids, prevGridSum, allGridSum, firstDevice);
  if (status != hipSuccess) {
    return status;
  }

  if (startEvent != nullptr) {
    hip::Event* eStart = reinterpret_cast<hip::Event*>(startEvent);
    status = eStart->addMarker(hStream, nullptr, false);
    if (status != hipSuccess) {
      return status;
    }
  }

  if (stopEvent != nullptr) {
    hip::Event* eStop = reinterpret_cast<hip::Event*>(stopEvent);
    if (eStop->flags & hipEventDisableSystemFence) {
      command->setEventScope(amd::Device::kCacheStateIgnore);
    } else {
      command->setEventScope(amd::Device::kCacheStateSystem);
    }
    // Enqueue Dispatch and bind the stop event
    command->enqueue();
    eStop->BindCommand(*command, false);
  } else {
    command->enqueue();
  }

  if (command->status() == CL_INVALID_OPERATION) {
    command->release();
    return hipErrorIllegalState;
  }

  command->release();

  return hipSuccess;
}

hipError_t hipModuleLaunchKernel(hipFunction_t f, uint32_t gridDimX, uint32_t gridDimY,
                                 uint32_t gridDimZ, uint32_t blockDimX, uint32_t blockDimY,
                                 uint32_t blockDimZ, uint32_t sharedMemBytes, hipStream_t hStream,
                                 void** kernelParams, void** extra) {
  HIP_INIT_API(hipModuleLaunchKernel, f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY,
               blockDimZ, sharedMemBytes, hStream, kernelParams, extra);

  if (!hip::isValid(hStream)) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  STREAM_CAPTURE(hipModuleLaunchKernel, hStream, f, gridDimX, gridDimY, gridDimZ, blockDimX,
                 blockDimY, blockDimZ, sharedMemBytes, kernelParams, extra);
  if (gridDimX > std::numeric_limits<int32_t>::max() ||
      gridDimY > std::numeric_limits<uint16_t>::max()+1 ||
      gridDimZ > std::numeric_limits<uint16_t>::max()+1) {
    HIP_RETURN(hipErrorInvalidValue);
  }
  size_t globalWorkSizeX = static_cast<size_t>(gridDimX) * blockDimX;
  size_t globalWorkSizeY = static_cast<size_t>(gridDimY) * blockDimY;
  size_t globalWorkSizeZ = static_cast<size_t>(gridDimZ) * blockDimZ;
  if (globalWorkSizeX > std::numeric_limits<uint32_t>::max()) {
    HIP_RETURN(hipErrorInvalidConfiguration);
  }
  HIP_RETURN(ihipModuleLaunchKernel(
      f, static_cast<uint32_t>(globalWorkSizeX), static_cast<uint32_t>(globalWorkSizeY),
      static_cast<uint32_t>(globalWorkSizeZ), blockDimX, blockDimY, blockDimZ, sharedMemBytes,
      hStream, kernelParams, extra, nullptr, nullptr));
}

hipError_t hipExtModuleLaunchKernel(hipFunction_t f, uint32_t globalWorkSizeX,
                                    uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ,
                                    uint32_t localWorkSizeX, uint32_t localWorkSizeY,
                                    uint32_t localWorkSizeZ, size_t sharedMemBytes,
                                    hipStream_t hStream, void** kernelParams, void** extra,
                                    hipEvent_t startEvent, hipEvent_t stopEvent, uint32_t flags) {
  HIP_INIT_API(hipExtModuleLaunchKernel, f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ,
               localWorkSizeX, localWorkSizeY, localWorkSizeZ, sharedMemBytes, hStream,
               kernelParams, extra, startEvent, stopEvent, flags);

  if (!hip::isValid(hStream)) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  STREAM_CAPTURE(hipExtModuleLaunchKernel, hStream, f, globalWorkSizeX, globalWorkSizeY,
                 globalWorkSizeZ, localWorkSizeX, localWorkSizeY, localWorkSizeZ, sharedMemBytes,
                 kernelParams, extra, startEvent, stopEvent, flags);

  HIP_RETURN(ihipModuleLaunchKernel(f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ,
                                    localWorkSizeX, localWorkSizeY, localWorkSizeZ, sharedMemBytes,
                                    hStream, kernelParams, extra, startEvent, stopEvent, flags));
}


hipError_t hipHccModuleLaunchKernel(hipFunction_t f, uint32_t globalWorkSizeX,
                                    uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ,
                                    uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                                    size_t sharedMemBytes, hipStream_t hStream, void** kernelParams,
                                    void** extra, hipEvent_t startEvent, hipEvent_t stopEvent) {
  HIP_INIT_API(hipHccModuleLaunchKernel, f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ,
               blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams, extra,
               startEvent, stopEvent);

  HIP_RETURN(ihipModuleLaunchKernel(f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ, blockDimX,
                                    blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams,
                                    extra, startEvent, stopEvent));
}

hipError_t hipModuleLaunchKernelExt(hipFunction_t f, uint32_t globalWorkSizeX,
                                    uint32_t globalWorkSizeY, uint32_t globalWorkSizeZ,
                                    uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                                    size_t sharedMemBytes, hipStream_t hStream, void** kernelParams,
                                    void** extra, hipEvent_t startEvent, hipEvent_t stopEvent) {
  HIP_INIT_API(hipModuleLaunchKernelExt, f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ,
               blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams, extra,
               startEvent, stopEvent);

  HIP_RETURN(ihipModuleLaunchKernel(f, globalWorkSizeX, globalWorkSizeY, globalWorkSizeZ, blockDimX,
                                    blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams,
                                    extra, startEvent, stopEvent));
}

hipError_t hipModuleLaunchCooperativeKernel(hipFunction_t f, unsigned int gridDimX,
                                            unsigned int gridDimY, unsigned int gridDimZ,
                                            unsigned int blockDimX, unsigned int blockDimY,
                                            unsigned int blockDimZ, unsigned int sharedMemBytes,
                                            hipStream_t stream, void** kernelParams) {
  HIP_INIT_API(hipModuleLaunchCooperativeKernel, f, gridDimX, gridDimY, gridDimZ, blockDimX,
               blockDimY, blockDimZ, sharedMemBytes, stream, kernelParams);

  if (!hip::isValid(stream)) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  size_t globalWorkSizeX = static_cast<size_t>(gridDimX) * blockDimX;
  size_t globalWorkSizeY = static_cast<size_t>(gridDimY) * blockDimY;
  size_t globalWorkSizeZ = static_cast<size_t>(gridDimZ) * blockDimZ;
  if (globalWorkSizeX > std::numeric_limits<uint32_t>::max() ||
      globalWorkSizeY > std::numeric_limits<uint32_t>::max() ||
      globalWorkSizeZ > std::numeric_limits<uint32_t>::max()) {
    HIP_RETURN(hipErrorInvalidConfiguration);
  }
  HIP_RETURN(ihipModuleLaunchKernel(f, static_cast<uint32_t>(globalWorkSizeX),
                  static_cast<uint32_t>(globalWorkSizeY),
                  static_cast<uint32_t>(globalWorkSizeZ), blockDimX, blockDimY,
                  blockDimZ, sharedMemBytes, stream, kernelParams, nullptr, nullptr,
                  nullptr, 0, amd::NDRangeKernelCommand::CooperativeGroups));
}

hipError_t ihipModuleLaunchCooperativeKernelMultiDevice(hipFunctionLaunchParams* launchParamsList,
                                                       unsigned int  numDevices,
                                                       unsigned int  flags,
                                                       uint32_t extFlags) {
  int numActiveGPUs = 0;
  hipError_t result = hipSuccess;
  result = ihipDeviceGetCount(&numActiveGPUs);

  if ((numDevices == 0) || (numDevices > numActiveGPUs)) {
    return hipErrorInvalidValue;
  }

  if (flags > (hipCooperativeLaunchMultiDeviceNoPostSync +
               hipCooperativeLaunchMultiDeviceNoPreSync)) {
    return hipErrorInvalidValue;
  }

  uint64_t allGridSize = 0;
  std::vector<const amd::Device*> mgpu_list(numDevices);

  for (int i = 0; i < numDevices; ++i) {
    uint32_t blockDims = 0;
    const hipFunctionLaunchParams& launch = launchParamsList[i];
    blockDims = launch.blockDimX * launch.blockDimY * launch.blockDimZ;
    allGridSize += launch.gridDimX * launch.gridDimY * launch.gridDimZ * blockDims;

    // Make sure block dimensions are valid
    if (0 == blockDims) {
      return hipErrorInvalidConfiguration;
    }
    if (launch.hStream != nullptr) {
      // Validate devices to make sure it dosn't have duplicates
      hip::Stream* hip_stream = reinterpret_cast<hip::Stream*>(launch.hStream);
      auto device = &hip_stream->vdev()->device();
      for (int j = 0; j < numDevices; ++j) {
        if (mgpu_list[j] == device) {
          return hipErrorInvalidDevice;
        }
      }
      mgpu_list[i] = device;
    } else {
      return hipErrorInvalidResourceHandle;
    }
  }
  uint64_t prevGridSize = 0;
  uint32_t firstDevice = 0;

  // Sync the execution streams on all devices
  if ((flags & hipCooperativeLaunchMultiDeviceNoPreSync) == 0) {
    for (int i = 0; i < numDevices; ++i) {
      hip::Stream* hip_stream =
          reinterpret_cast<hip::Stream*>(launchParamsList[i].hStream);
      hip_stream->finish();
    }
  }

  for (int i = 0; i < numDevices; ++i) {
    const hipFunctionLaunchParams& launch = launchParamsList[i];
    hip::Stream* hip_stream = reinterpret_cast<hip::Stream*>(launch.hStream);

    if (i == 0) {
      // The order of devices in the launch may not match the order in the global array
      for (size_t dev = 0; dev < g_devices.size(); ++dev) {
        // Find the matching device
        if (&hip_stream->vdev()->device() == g_devices[dev]->devices()[0]) {
          // Save ROCclr index of the first device in the launch
          firstDevice = hip_stream->vdev()->device().index();
          break;
        }
      }
    }

    size_t globalWorkSizeX = static_cast<size_t>(launch.gridDimX) * launch.blockDimX;
    size_t globalWorkSizeY = static_cast<size_t>(launch.gridDimY) * launch.blockDimY;
    size_t globalWorkSizeZ = static_cast<size_t>(launch.gridDimZ) * launch.blockDimZ;
    if (globalWorkSizeX > std::numeric_limits<uint32_t>::max() ||
        globalWorkSizeY > std::numeric_limits<uint32_t>::max() ||
        globalWorkSizeZ > std::numeric_limits<uint32_t>::max()) {
      return hipErrorInvalidConfiguration;
    }
    result = ihipModuleLaunchKernel(
        launch.function, static_cast<uint32_t>(globalWorkSizeX),
        static_cast<uint32_t>(globalWorkSizeY),
        static_cast<uint32_t>(globalWorkSizeZ), launch.blockDimX, launch.blockDimY,
        launch.blockDimZ, launch.sharedMemBytes, launch.hStream, launch.kernelParams,
        nullptr, nullptr, nullptr, flags, extFlags,
        i, numDevices, prevGridSize, allGridSize, firstDevice);
    if (result != hipSuccess) {
      break;
    }
    prevGridSize += globalWorkSizeX * globalWorkSizeY * globalWorkSizeZ;
  }

  // Sync the execution streams on all devices
  if ((flags & hipCooperativeLaunchMultiDeviceNoPostSync) == 0) {
    for (int i = 0; i < numDevices; ++i) {
      hip::Stream* hip_stream =
          reinterpret_cast<hip::Stream*>(launchParamsList[i].hStream);
      hip_stream->finish();
    }
  }

  return result;
}

hipError_t hipModuleLaunchCooperativeKernelMultiDevice(hipFunctionLaunchParams* launchParamsList,
                                                       unsigned int  numDevices,
                                                       unsigned int  flags) {
  HIP_INIT_API(hipModuleLaunchCooperativeKernelMultiDevice, launchParamsList, numDevices, flags);

  if (launchParamsList == nullptr) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  // Validate all streams passed by user
  for (int i = 0; i < numDevices; ++i) {
    if (!hip::isValid(launchParamsList[i].hStream)) {
      HIP_RETURN(hipErrorInvalidValue);
    }
  }

  HIP_RETURN(ihipModuleLaunchCooperativeKernelMultiDevice(
      launchParamsList,
      numDevices,
      flags,
      (amd::NDRangeKernelCommand::CooperativeGroups |
      amd::NDRangeKernelCommand::CooperativeMultiDeviceGroups)));

}

extern "C" hipError_t hipLaunchKernel_common(const void* hostFunction, dim3 gridDim, dim3 blockDim,
                                             void** args, size_t sharedMemBytes,
                                             hipStream_t stream) {
  STREAM_CAPTURE(hipLaunchKernel, stream, hostFunction, gridDim, blockDim, args, sharedMemBytes);
  return ihipLaunchKernel(hostFunction, gridDim, blockDim, args, sharedMemBytes, stream, nullptr,
                          nullptr, 0);
}

extern "C" hipError_t hipLaunchKernel(const void* hostFunction, dim3 gridDim, dim3 blockDim,
                                      void** args, size_t sharedMemBytes, hipStream_t stream) {
  HIP_INIT_API(hipLaunchKernel, hostFunction, gridDim, blockDim, args, sharedMemBytes, stream);
  HIP_RETURN(hipLaunchKernel_common(hostFunction, gridDim, blockDim, args, sharedMemBytes, stream));
}

extern "C" hipError_t hipLaunchKernel_spt(const void* hostFunction, dim3 gridDim, dim3 blockDim,
                                          void** args, size_t sharedMemBytes, hipStream_t stream) {
  HIP_INIT_API(hipLaunchKernel, hostFunction, gridDim, blockDim, args, sharedMemBytes, stream);
  PER_THREAD_DEFAULT_STREAM(stream);
  HIP_RETURN(hipLaunchKernel_common(hostFunction, gridDim, blockDim, args, sharedMemBytes, stream));
}

extern "C" hipError_t hipExtLaunchKernel(const void* hostFunction, dim3 gridDim, dim3 blockDim,
                                         void** args, size_t sharedMemBytes, hipStream_t stream,
                                         hipEvent_t startEvent, hipEvent_t stopEvent, int flags) {
  HIP_INIT_API(hipExtLaunchKernel, hostFunction, gridDim, blockDim, args, sharedMemBytes,
               stream, startEvent, stopEvent, flags);

  if (!hip::isValid(stream) || !hip::isValid(startEvent) || !hip::isValid(stopEvent)) {
    HIP_RETURN(hipErrorInvalidValue);
  }

  STREAM_CAPTURE(hipExtLaunchKernel, stream, hostFunction, gridDim, blockDim, args, sharedMemBytes,
                 startEvent, stopEvent, flags);
  HIP_RETURN(ihipLaunchKernel(hostFunction, gridDim, blockDim, args, sharedMemBytes, stream,
                              startEvent, stopEvent, flags));
}

hipError_t hipLaunchCooperativeKernel_common(const void* f, dim3 gridDim, dim3 blockDim,
                                             void** kernelParams, uint32_t sharedMemBytes,
                                             hipStream_t hStream) {
  if (!hip::isValid(hStream)) {
    return hipErrorInvalidValue;
  }

  hipFunction_t func = nullptr;
  int deviceId = hip::Stream::DeviceId(hStream);
  hipError_t getStatFuncError = PlatformState::instance().getStatFunc(&func, f, deviceId);
  if (getStatFuncError != hipSuccess) {
    return getStatFuncError;
  }
  const amd::Device* device = g_devices[deviceId]->devices()[0];
  size_t globalWorkSizeX = static_cast<size_t>(gridDim.x) * blockDim.x;
  size_t globalWorkSizeY = static_cast<size_t>(gridDim.y) * blockDim.y;
  size_t globalWorkSizeZ = static_cast<size_t>(gridDim.z) * blockDim.z;
  if (globalWorkSizeX > std::numeric_limits<uint32_t>::max() ||
      globalWorkSizeY > std::numeric_limits<uint32_t>::max() ||
      globalWorkSizeZ > std::numeric_limits<uint32_t>::max() ||
      (blockDim.x * blockDim.y * blockDim.z > device->info().maxWorkGroupSize_)) {
    return hipErrorInvalidConfiguration;
  }

  return ihipModuleLaunchKernel(func, static_cast<uint32_t>(globalWorkSizeX),
                                static_cast<uint32_t>(globalWorkSizeY),
                                static_cast<uint32_t>(globalWorkSizeZ), blockDim.x, blockDim.y,
                                blockDim.z, sharedMemBytes, hStream, kernelParams, nullptr, nullptr,
                                nullptr, 0, amd::NDRangeKernelCommand::CooperativeGroups);
}

hipError_t hipLaunchCooperativeKernel(const void* f, dim3 gridDim, dim3 blockDim,
                                      void** kernelParams, uint32_t sharedMemBytes,
                                      hipStream_t hStream) {
  HIP_INIT_API(hipLaunchCooperativeKernel, f, gridDim, blockDim, sharedMemBytes, hStream);
  HIP_RETURN(hipLaunchCooperativeKernel_common(f, gridDim, blockDim, kernelParams, sharedMemBytes,
                                               hStream));
}

hipError_t hipLaunchCooperativeKernel_spt(const void* f, dim3 gridDim, dim3 blockDim,
                                          void** kernelParams, uint32_t sharedMemBytes,
                                          hipStream_t hStream) {
  HIP_INIT_API(hipLaunchCooperativeKernel, f, gridDim, blockDim, sharedMemBytes, hStream);
  PER_THREAD_DEFAULT_STREAM(hStream);
  HIP_RETURN(hipLaunchCooperativeKernel_common(f, gridDim, blockDim, kernelParams, sharedMemBytes,
                                               hStream));
}

hipError_t ihipLaunchCooperativeKernelMultiDevice(hipLaunchParams* launchParamsList, int numDevices,
                                                  unsigned int flags, uint32_t extFlags) {
  if (launchParamsList == nullptr) {
    return hipErrorInvalidValue;
  }

  std::vector<hipFunctionLaunchParams> functionLaunchParamsList(numDevices);
  // Convert hipLaunchParams to hipFunctionLaunchParams
  for (int i = 0; i < numDevices; ++i) {
    hipLaunchParams& launch = launchParamsList[i];
    // Validate stream passed by user
    if (!hip::isValid(launch.stream)) {
      return hipErrorInvalidValue;
    }

    hip::Stream* hip_stream = hip::getStream(launch.stream);
    hipFunction_t func = nullptr;
    // The order of devices in the launch may not match the order in the global array
    for (size_t dev = 0; dev < g_devices.size(); ++dev) {
      // Find the matching device and request the kernel function
      if (&hip_stream->vdev()->device() == g_devices[dev]->devices()[0]) {
        IHIP_RETURN_ONFAIL(PlatformState::instance().getStatFunc(&func, launch.func, dev));
        break;
      }
    }
    if (func == nullptr) {
      return hipErrorInvalidDeviceFunction;
    }

    functionLaunchParamsList[i].function = func;
    functionLaunchParamsList[i].gridDimX = launch.gridDim.x;
    functionLaunchParamsList[i].gridDimY = launch.gridDim.y;
    functionLaunchParamsList[i].gridDimZ = launch.gridDim.z;
    functionLaunchParamsList[i].blockDimX = launch.blockDim.x;
    functionLaunchParamsList[i].blockDimY = launch.blockDim.y;
    functionLaunchParamsList[i].blockDimZ = launch.blockDim.z;
    functionLaunchParamsList[i].sharedMemBytes = launch.sharedMem;
    functionLaunchParamsList[i].hStream = launch.stream;
    functionLaunchParamsList[i].kernelParams = launch.args;
  }

  return ihipModuleLaunchCooperativeKernelMultiDevice(functionLaunchParamsList.data(),
                                                      functionLaunchParamsList.size(),
                                                      flags,
                                                      extFlags);
}

hipError_t hipLaunchCooperativeKernelMultiDevice(hipLaunchParams* launchParamsList, int numDevices,
                                                 unsigned int flags) {
  HIP_INIT_API(hipLaunchCooperativeKernelMultiDevice, launchParamsList, numDevices, flags);

  HIP_RETURN(ihipLaunchCooperativeKernelMultiDevice(
      launchParamsList, numDevices, flags,
      (amd::NDRangeKernelCommand::CooperativeGroups |
       amd::NDRangeKernelCommand::CooperativeMultiDeviceGroups)));
}

hipError_t hipExtLaunchMultiKernelMultiDevice(hipLaunchParams* launchParamsList, int numDevices,
                                              unsigned int flags) {
  HIP_INIT_API(hipExtLaunchMultiKernelMultiDevice, launchParamsList, numDevices, flags);

  HIP_RETURN(ihipLaunchCooperativeKernelMultiDevice(launchParamsList, numDevices, flags, 0));
}

hipError_t hipModuleGetTexRef(textureReference** texRef, hipModule_t hmod, const char* name) {
  HIP_INIT_API(hipModuleGetTexRef, texRef, hmod, name);

  /* input args check */
  if ((texRef == nullptr) || (name == nullptr) || (strlen(name) == 0)) {
    HIP_RETURN(hipErrorInvalidValue);
  }
  if (hmod == nullptr) {
    HIP_RETURN(hipErrorInvalidResourceHandle);
  }

  amd::Device* device = hip::getCurrentDevice()->devices()[0];
  const device::Info& info = device->info();
  if (!info.imageSupport_) {
    LogPrintfError("Texture not supported on the device %s", info.name_);
    HIP_RETURN(hipErrorNotSupported);
  }

  /* Get address and size for the global symbol */
  if (hipSuccess != PlatformState::instance().getDynTexRef(name, hmod, texRef)) {
    LogPrintfError("Cannot get texRef for name: %s at module:0x%x \n", name, hmod);
    HIP_RETURN(hipErrorNotFound);
  }

  // Texture references created by HIP driver API
  // have the default read mode set to normalized float.
  // have format set to format float
  // set num of channels to 1
  (*texRef)->readMode = hipReadModeNormalizedFloat;
  (*texRef)->format = HIP_AD_FORMAT_FLOAT;
  (*texRef)->numChannels = 1;

  hipError_t err = PlatformState::instance().registerTexRef(*texRef, hmod, std::string(name));

  HIP_RETURN(err);
}
