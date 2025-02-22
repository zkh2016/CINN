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

// Copyright (c) 202 CINN Authors. All Rights Reserved.
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

#include <memory>

#include "cinn/frontend/syntax.h"
#include "cinn/hlir/framework/graph.h"
#include "cinn/hlir/framework/graph_compiler.h"
#include "cinn/hlir/framework/pass.h"
#include "cinn/utils/data_util.h"

DEFINE_string(model_dir, "", "");

namespace cinn {
namespace frontend {

using hlir::framework::Scope;
using utils::Join;

TEST(common_subexpression_elimination, common_subexpression_elimination_case1) {
  Placeholder A(Float(32), {32, 16, 1}, "A");
  Placeholder B(Float(32), {32, 1, 1}, "B", true);

  Program program;
  auto add_1  = program.add(A, B);
  auto add_2  = program.add(B, A);
  auto add    = program.add(add_1, add_2);
  auto t_1    = program.transpose(add, {2, 1, 0});  // {1, 16, 32}
  auto t_2    = program.transpose(add, {2, 1, 0});  // {1, 16, 32}
  auto t_3    = program.transpose(add, {2, 1, 0});  // {1, 16, 32}
  auto concat = program.concat({t_1, t_2, t_3});
  auto max    = program.reduce_max(concat, {0}, true);

  Target target = common::DefaultTarget();
  program.SetInputs({A, B});
  program.Validate();
  LOG(INFO) << "Program:\n" << program;
  auto graph = std::make_shared<hlir::framework::Graph>(program, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  hlir::framework::ApplyPass(graph.get(), "CommonSubexpressionEliminationPass");
  auto scope = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  auto& prerun_instrs  = runtime_program->GetPreRunInstructions();
  auto& run_instrs     = runtime_program->GetRunInstructions();
  ASSERT_EQ(prerun_instrs.size(), 0);
  ASSERT_EQ(run_instrs.size(), 5);

  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");

  auto A1 = scope->GetTensor("A");
  auto B1 = scope->GetTensor("B");
  SetRandData<float>(A1, target);
  SetRandData<float>(B1, target);

  LOG(INFO) << "graph:\n" << graph->Visualize();
  runtime_program->Execute();
}

TEST(common_subexpression_elimination, common_subexpression_elimination_case2) {
  Placeholder A(Float(32), {32, 16}, "A");
  Placeholder B(Float(32), {32, 1}, "B", true);

  Program program;
  auto add_1     = program.add(A, A);
  auto add_2     = program.add(A, A);
  auto reshape_1 = program.reshape(B, {4, -1});
  auto reshape_2 = program.reshape(B, {4, 8});
  auto concat_1  = program.concat({reshape_1, reshape_2});
  auto concat_2  = program.concat({reshape_1, reshape_2});
  auto concat_3  = program.concat({reshape_1, reshape_2}, 1);

  Target target = common::DefaultTarget();
  program.SetInputs({A, B});
  program.Validate();
  LOG(INFO) << "Program:\n" << program;
  auto graph = std::make_shared<hlir::framework::Graph>(program, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  hlir::framework::ApplyPass(graph.get(), "CommonSubexpressionEliminationPass");
  auto scope = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  auto& prerun_instrs  = runtime_program->GetPreRunInstructions();
  auto& run_instrs     = runtime_program->GetRunInstructions();
  ASSERT_EQ(prerun_instrs.size(), 0);
  ASSERT_EQ(run_instrs.size(), 4);

  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");

  auto A1 = scope->GetTensor("A");
  auto B1 = scope->GetTensor("B");
  SetRandData<float>(A1, target);
  SetRandData<float>(B1, target);

  LOG(INFO) << "graph:\n" << graph->Visualize();
  runtime_program->Execute();
}

#ifdef CINN_WITH_CUDA
TEST(common_subexpression_elimination, common_subexpression_elimination_case3) {
  Placeholder A(Float(32), {1, 3, 224, 224}, "A");
  Placeholder B(Float(32), {1, 1, 224, 224}, "B", true);

  absl::flat_hash_map<std::string, Program::attr_t> attrs;
  attrs["stride"]        = std::vector<int>({2, 2});
  attrs["dilation"]      = std::vector<int>({1, 1});
  attrs["padding"]       = std::vector<int>({3, 3});
  std::string src_layout = "NCHW";
  attrs["data_format"]   = src_layout;

  Program program;
  auto add_1    = program.add(A, B);
  auto weight_1 = program.fill_constant<float>({64, 3, 7, 7}, 1.0f, "", false, "w1");
  auto weight_2 = program.fill_constant<float>({64, 3, 7, 7}, 1.0f, "", false, "w2");
  auto bias     = program.fill_constant<float>({1, 64, 112, 112}, 2.0f, "", false, "b1");
  auto conv_1   = program.conv2d(add_1, weight_1, attrs);
  auto add_2    = program.add(conv_1, bias);
  auto relu_1   = program.relu(add_2);
  auto conv_2   = program.conv2d(add_1, weight_2, attrs);
  auto add_3    = program.add(conv_2, bias);
  auto relu_2   = program.relu(add_3);
  auto out1     = program.add(relu_1, add_2);
  auto out2     = program.add(add_2, relu_2);

  Target target = common::DefaultTarget();
  program.SetInputs({A, B});
  program.Validate();
  LOG(INFO) << "Program:\n" << program;
  std::unordered_set<std::string> fetch_list;
  fetch_list.insert(out1->id);
  fetch_list.insert(out2->id);
  auto graph = std::make_shared<hlir::framework::Graph>(program, fetch_list, target);
  LOG(INFO) << "graph:\n" << graph->Visualize();

  hlir::framework::ApplyPass(graph.get(), "InferShape");
  hlir::framework::ApplyPass(graph.get(), "CommonSubexpressionEliminationPass");
  auto scope = BuildScope(target, graph);

  hlir::framework::GraphCompiler gc(target, scope, graph);
  auto runtime_program = gc.Build();
  auto& prerun_instrs  = runtime_program->GetPreRunInstructions();
  auto& run_instrs     = runtime_program->GetRunInstructions();
  ASSERT_EQ(prerun_instrs.size(), 0);
  ASSERT_EQ(run_instrs.size(), 7);
  scope->Var<hlir::framework::Tensor>("A");
  scope->Var<hlir::framework::Tensor>("B");

  auto A1 = scope->GetTensor("A");
  auto B1 = scope->GetTensor("B");
  SetRandData<float>(A1, target);
  SetRandData<float>(B1, target);

  LOG(INFO) << "graph:\n" << graph->Visualize();
  runtime_program->Execute();
}
#endif

}  // namespace frontend
}  // namespace cinn
