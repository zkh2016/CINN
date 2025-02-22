// Copyright (c) 2022 CINN Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <cfloat>

#include "cinn/cinn.h"
#include "cinn/frontend/net_builder.h"
#include "cinn/frontend/pass/pass_test_helper.h"
#include "cinn/frontend/pass/use_program_pass.h"
#include "cinn/frontend/program_pass.h"
#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/pass.h"
#include "cinn/hlir/op/use_ops.h"
#include "cinn/hlir/pass/use_pass.h"
#include "cinn/utils/data_util.h"

namespace cinn::frontend {

TEST(AutoCast, Exp) {
  NetBuilder builder("net_builder");
  auto x       = builder.CreateInput(Float(16, 1, common::Type::specific_type_t::FP16), {4, 5, 3}, "X");
  auto out     = builder.Exp(x);
  auto program = builder.Build();

  common::Target target = common::DefaultNVGPUTarget();
  std::pair<std::vector<std::string>, std::vector<std::string>> passes{{}, {"AutoCast", "Decomposer"}};
  CompareProgramPassResult(&program, target, {out->id}, -2, passes);
}

TEST(AutoCast, BatchNorm) {
  NetBuilder builder("net_builder");
  auto x        = builder.CreateInput(Float(16, 1, common::Type::specific_type_t::FP16), {128, 64, 112, 112}, "X");
  auto scale    = builder.FillConstant({64}, 1.0f, "scale", "float32");
  auto bias     = builder.FillConstant({64}, 0.0f, "bias", "float32");
  auto mean     = builder.FillConstant({64}, 0.0f, "mean", "float32");
  auto variance = builder.FillConstant({64}, 1.0f, "variance", "float32");
  auto out      = builder.BatchNorm(x, scale, bias, mean, variance, 1e-5f, 0.9f, "NCHW", false);
  auto program  = builder.Build();

  common::Target target = common::DefaultNVGPUTarget();
  std::pair<std::vector<std::string>, std::vector<std::string>> passes{{}, {"AutoCast", "Decomposer"}};
  CompareProgramPassResult(&program, target, {out[0]->id}, -2, passes);
}

}  // namespace cinn::frontend
