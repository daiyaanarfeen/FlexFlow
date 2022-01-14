/* Copyright 2020 Stanford
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

#include <hip/hip_runtime.h>
#include "flexflow/ops/dropout.h"
#include "flexflow/utils/hip_helper.h"

namespace FlexFlow {

// declare Legion names
using Legion::Memory;
using Legion::Domain;
using Legion::coord_t;

void Dropout::forward_kernel(DropoutMeta *m,
                             float const *input_ptr,
                             float *output_ptr,
                             hipStream_t stream)
{
  checkCUDNN(miopenSetStream(m->handle.dnn, stream));

#if 0
  checkCUDNN(cudnnDropoutForward(m->handle.dnn, m->dropoutDesc,
      m->inputTensor, input_ptr, m->outputTensor, output_ptr,
      m->reserveSpace, m->reserveSpaceSize));
#endif
}

/*static*/
void Dropout::forward_kernel_wrapper(DropoutMeta *m,
                                     float const *input_ptr,
                                     float *output_ptr)
{
  hipStream_t stream;
  checkCUDA(get_legion_stream(&stream));
  Dropout::forward_kernel(m, input_ptr, output_ptr, stream);
}

void Dropout::backward_kernel(DropoutMeta *m,
                              float const *output_grad_ptr,
                              float *input_grad_ptr,
                              hipStream_t stream)
{
  checkCUDNN(miopenSetStream(m->handle.dnn, stream));

#if 0
  checkCUDNN(cudnnDropoutBackward(m->handle.dnn, m->dropoutDesc,
      m->outputTensor, output_grad_ptr, m->inputTensor, input_grad_ptr,
      m->reserveSpace, m->reserveSpaceSize));
#endif
}

/*static*/
void Dropout::backward_kernel_wrapper(DropoutMeta *m,
                                      float const *output_grad_ptr,
                                      float *input_grad_ptr)
{
  hipStream_t stream;
  checkCUDA(get_legion_stream(&stream));
  Dropout::backward_kernel(m, output_grad_ptr, input_grad_ptr, stream);
}

DropoutMeta::DropoutMeta(FFHandler handler,
                         const Dropout* dropout,
                         Memory gpu_mem,
                         const Domain& output_domain)
: OpMeta(handler)
{
#if 0
  profiling = dropout->profiling;
  checkCUDNN(hipdnnCreateTensorDescriptor(&inputTensor));
  checkCUDNN(hipdnnCreateTensorDescriptor(&outputTensor));
  checkCUDNN(hipdnnCreateDropoutDescriptor(&dropoutDesc));
  checkCUDNN(hipdnnDropoutGetStatesSize(handle.dnn, &(dropoutStateSize)));
  checkCUDNN(cudnnSetTensorDescriptorFromDomain(inputTensor, output_domain));
  checkCUDNN(cudnnSetTensorDescriptorFromDomain(outputTensor, output_domain));
  checkCUDNN(cudnnDropoutGetReserveSpaceSize(outputTensor, &(reserveSpaceSize)));
  {
    // allocate memory for dropoutStates and reserveSpace
    size_t totalSize = dropoutStateSize + reserveSpaceSize;
    Realm::Rect<1, coord_t> bounds(Realm::Point<1, coord_t>(0),
        Realm::Point<1, coord_t>(totalSize-1));
    std::vector<size_t> field_sizes;
    field_sizes.push_back(sizeof(char));
    Realm::RegionInstance::create_instance(reserveInst, gpu_mem, bounds,
        field_sizes, 0, Realm::ProfilingRequestSet()).wait();
    dropoutStates = reserveInst.pointer_untyped(0, sizeof(char));
    reserveSpace = ((char*)dropoutStates) + dropoutStateSize;
  }
  //checkCUDA(hipMalloc(&dropoutStates, dropoutStateSize));
  //checkCUDA(hipMalloc(&reserveSpace, reserveSpaceSize));
  checkCUDNN(hipdnnSetDropoutDescriptor(
    dropoutDesc, handle.dnn, dropout->rate, dropoutStates, dropoutStateSize, dropout->seed
  ));
#endif
}

DropoutMeta::~DropoutMeta(void)
{
#if 0
  reserveInst.destroy();
  checkCUDNN(hipdnnDestroyTensorDescriptor(inputTensor));
  checkCUDNN(hipdnnDestroyTensorDescriptor(outputTensor));
  checkCUDNN(hipdnnDestroyDropoutDescriptor(dropoutDesc));
#endif
}

}; // namespace