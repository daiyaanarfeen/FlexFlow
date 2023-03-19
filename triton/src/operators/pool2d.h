/* Copyright 2022 NVIDIA CORPORATION
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __LEGION_TRITON_POOL2D_H__
#define __LEGION_TRITON_POOL2D_H__

#include "operator.h"
#include "tensor.h"

namespace triton {
namespace backend {
namespace legion {

struct Pool2DArgs : public OperatorArgs {
public:
  Pool2DArgs(void);
#ifdef LEGION_USE_CUDA
  cudnnTensorDescriptor_t inputTensor, outputTensor;
  cudnnActivationDescriptor_t actiDesc;
  cudnnPoolingDescriptor_t poolDesc;
#endif
  bool relu;
};

class Pool2D : public Operator {
public:
  Pool2D(LegionModelState *model,
         LayerStrategy const *strategy,
         int kernelH,
         int kernelW,
         int strideH,
         int strideW,
         int paddingH,
         int paddingW,
         PoolType type,
         ActivationMode activation,
         char const *name);
  virtual ~Pool2D(void);

  void Configure(Tensor *input, Tensor *output);

  virtual void Load(Realm::Processor processor) override;
  virtual void initialize(LegionModelInstance *instance,
                          unsigned const instance_index,
                          Legion::Runtime *runtime,
                          Legion::Context ctx,
                          Legion::MapperID mapper) override;
  virtual void forward(LegionModelInstance *instance,
                       unsigned const instance_index,
                       Legion::Runtime *runtime,
                       Legion::Context ctx,
                       Legion::MapperID mapper) override;
  virtual void finalize(LegionModelInstance *instance,
                        unsigned const instance_index,
                        Legion::Runtime *runtime,
                        Legion::Context ctx,
                        Legion::MapperID mapper) override;
  virtual void Free(Realm::Processor processor) override;

#ifdef LEGION_USE_CUDA
  static Pool2DArgs
      initialize_gpu(Legion::Task const *task,
                     std::vector<Legion::PhysicalRegion> const &regions,
                     Legion::Context ctx,
                     Legion::Runtime *runtime);
  static void forward_gpu(Legion::Task const *task,
                          std::vector<Legion::PhysicalRegion> const &regions,
                          Legion::Context ctx,
                          Legion::Runtime *runtime);
  static void forward_kernel(Pool2DArgs const *args,
                             void const *input_ptr,
                             void *output_ptr);
#endif
public:
  const ActivationMode activation;
  const PoolType pool_type;
  int const kernel_h, kernel_w, stride_h, stride_w, padding_h, padding_w;
};

} // namespace legion
} // namespace backend
} // namespace triton

#endif // __LEGION_TRITON_POOL2D_H__
