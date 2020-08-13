# Copyright 2020 Stanford University, Los Alamos National Laboratory
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# 
from flexflow.keras.models import Model, Sequential
from flexflow.keras.layers import Input, Flatten, Dense, Activation, Conv2D, MaxPooling2D, Concatenate, concatenate
import flexflow.keras.optimizers
from flexflow.keras.datasets import mnist
from flexflow.keras.datasets import cifar10
from flexflow.keras import losses
from flexflow.keras import metrics
from flexflow.keras.callbacks import Callback, VerifyMetrics
from example.accuracy import ModelAccuracy

import flexflow.core as ff
import numpy as np
import argparse
import gc
  
def cifar_cnn_sub(input_tensor, name_postfix):
  name = "conv2d_0_" + str(name_postfix)
  t1 = Conv2D(filters=32, input_shape=(3,32,32), kernel_size=(3,3), strides=(1,1), padding=(1,1), activation="relu", name=name)(input_tensor)
  name = "conv2d_1_" + str(name_postfix)
  ot1 = Conv2D(filters=32, kernel_size=(3,3), strides=(1,1), padding=(1,1), activation="relu", name=name)(t1)
  return ot1
    
def top_level_task():
  num_classes = 10
  
  num_samples = 10000
  
  (x_train, y_train), (x_test, y_test) = cifar10.load_data(num_samples)
  
  x_train = x_train.astype('float32')
  x_train /= 255
  y_train = y_train.astype('int32')
  print("shape: ", x_train.shape)
  
  input_tensor1 = Input(shape=(3, 32, 32), dtype="float32", name="input1")
  input_tensor2 = Input(shape=(3, 32, 32), dtype="float32", name="input2")

  ot1 = cifar_cnn_sub(input_tensor1, 1)
  model1 = Model(input_tensor1, ot1)
  print(model1.summary())
  ot2 = cifar_cnn_sub(input_tensor2, 2)
  model2 = Model(input_tensor2, ot2)
  print(model2.summary())
  output_tensor = Concatenate(axis=1)([model1.output, model2.output])
  output_tensor = MaxPooling2D(pool_size=(2,2), strides=(2,2), padding="valid")(output_tensor)
  output_tensor = Conv2D(filters=64, kernel_size=(3,3), strides=(1,1), padding=(1,1), activation="relu", name="conv2d_0_4")(output_tensor)
  output_tensor = Conv2D(filters=64, kernel_size=(3,3), strides=(1,1), padding=(1,1), activation="relu")(output_tensor)
  output_tensor = MaxPooling2D(pool_size=(2,2), strides=(2,2), padding="valid")(output_tensor)
  output_tensor = Flatten()(output_tensor)
  output_tensor = Dense(512, activation="relu")(output_tensor)
  output_tensor = Dense(num_classes)(output_tensor)
  output_tensor = Activation("softmax")(output_tensor)

  model = Model([input_tensor1, input_tensor2], output_tensor)
 # print(model.summary())
  
  opt = flexflow.keras.optimizers.SGD(learning_rate=0.001)
  model.compile(optimizer=opt, loss='sparse_categorical_crossentropy', metrics=['accuracy', 'sparse_categorical_crossentropy'])
  print(model.summary())

  model.fit([x_train, x_train], y_train, epochs=1, callbacks=[VerifyMetrics(ModelAccuracy.CIFAR10_CNN)])

if __name__ == "__main__":
  print("Functional API, cifar10 cnn concat model")
  top_level_task()
  gc.collect()