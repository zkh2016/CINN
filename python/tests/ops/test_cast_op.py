#!/usr/bin/env python3

# Copyright (c) 2021 CINN Authors. All Rights Reserved.
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

import unittest
import numpy as np
from op_test import OpTest, OpTestTool
import paddle
import cinn
from cinn.frontend import *
from cinn.common import *


@OpTestTool.skip_if(not is_compiled_with_cuda(),
                    "x86 test will be skipped due to timeout.")
class TestCastOp(OpTest):
    def setUp(self):
        self.init_case()

    def init_case(self):
        self.inputs = {"x": self.random([10201, 50], "float32", 1, 10)}
        self.dtype = "int64"

    def build_paddle_program(self, target):
        x = paddle.to_tensor(self.inputs["x"], stop_gradient=True)
        out = paddle.cast(x, self.dtype)

        self.paddle_outputs = [out]

    # Note: If the forward and backward operators are run in the same program,
    # the forward result will be incorrect.
    def build_cinn_program(self, target):
        builder = NetBuilder("cast")
        x = builder.create_input(
            self.nptype2cinntype(self.inputs["x"].dtype),
            self.inputs["x"].shape, "x")
        out = builder.cast(x, self.dtype)

        prog = builder.build()
        res = self.get_cinn_output(prog, target, [x], [self.inputs["x"]],
                                   [out])

        self.cinn_outputs = res

    def test_check_results(self):
        self.check_outputs_and_grads(all_equal=True)


class TestCastCase1(TestCastOp):
    def init_case(self):
        self.inputs = {"x": self.random([10201, 50], "float32", 1, 10)}
        self.dtype = "float32"


class TestCastCase2(TestCastOp):
    def init_case(self):
        self.inputs = {"x": self.random([32, 64], "int32", 1, 10)}
        self.dtype = "uint8"


class TestCastCase3(TestCastOp):
    def init_case(self):
        self.inputs = {"x": self.random([32, 64], "uint8", 1, 10)}
        self.dtype = "int32"


if __name__ == "__main__":
    unittest.main()
