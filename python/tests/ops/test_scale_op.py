# Copyright (c) 2023 CINN Authors. All Rights Reserved.
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
from op_test_helper import TestCaseHelper
import paddle
import cinn
from cinn.frontend import *
from cinn.common import *


@OpTestTool.skip_if(not is_compiled_with_cuda(),
                    "x86 test will be skipped due to timeout.")
class TestScaleOp(OpTest):
    def setUp(self):
        print(f"\nRunning {self.__class__.__name__}: {self.case}")
        self.prepare_inputs()

    def prepare_inputs(self):
        self.x_np = self.random(
            shape=self.case["x_shape"], dtype=self.case["x_dtype"])

    def build_paddle_program(self, target):
        x = paddle.to_tensor(self.x_np, stop_gradient=True)
        out = paddle.scale(x, self.case["scale"], self.case["bias"],
                           self.case["bias_after_scale"])
        self.paddle_outputs = [out]

    def build_cinn_program(self, target):
        builder = NetBuilder("scale")
        x = builder.create_input(
            self.nptype2cinntype(self.case["x_dtype"]), self.case["x_shape"],
            "x")
        out = builder.scale(x, self.case["scale"], self.case["bias"],
                            self.case["bias_after_scale"])

        prog = builder.build()
        res = self.get_cinn_output(prog, target, [x], [self.x_np], [out])
        self.cinn_outputs = [res[0]]

    def test_check_results(self):
        max_relative_error = self.case[
            "max_relative_error"] if "max_relative_error" in self.case else 1e-5
        self.check_outputs_and_grads(max_relative_error=max_relative_error)


class TestScaleAll(TestCaseHelper):
    def init_attrs(self):
        self.class_name = "TestScaleOpCase"
        self.cls = TestScaleOp
        self.inputs = [{
            "x_shape": [1],
        }, {
            "x_shape": [1024],
        }, {
            "x_shape": [512, 256],
        }, {
            "x_shape": [128, 64, 32],
        }, {
            "x_shape": [16, 8, 4, 2],
        }, {
            "x_shape": [16, 8, 4, 2, 1],
        }]
        self.dtypes = [
            # {
            #     # Todo: Support uint8 in op scale
            #     "x_dtype": "uint8",
            # },
            {
                "x_dtype": "int8",
            },
            {
                "x_dtype": "int16"
            },
            {
                "x_dtype": "int32",
            },
            {
                "x_dtype": "int64"
            },
            {
                "x_dtype": "float16",
                "max_relative_error": 1e-3
            },
            {
                "x_dtype": "float32",
            },
            {
                "x_dtype": "float64",
            },
        ]
        self.attrs = [{
            "scale": 0,
            "bias": 0,
            "bias_after_scale": True
        }, {
            "scale": 0,
            "bias": 0,
            "bias_after_scale": False
        }, {
            "scale": 0.1,
            "bias": 10,
            "bias_after_scale": True
        }, {
            "scale": -0.1,
            "bias": 10,
            "bias_after_scale": False
        }, {
            "scale": 1,
            "bias": 0,
            "bias_after_scale": True
        }, {
            "scale": -1,
            "bias": 0,
            "bias_after_scale": False
        }, {
            "scale": 0,
            "bias": 10,
            "bias_after_scale": True
        }, {
            "scale": 0,
            "bias": 10,
            "bias_after_scale": False
        }]


if __name__ == "__main__":
    TestScaleAll().run()
