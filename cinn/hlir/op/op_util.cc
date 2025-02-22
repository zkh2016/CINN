// Copyright (c) 2021 CINN Authors. All Rights Reserved.
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

#include "cinn/hlir/op/op_util.h"

#include "cinn/hlir/pe/ir_schedule_pe.h"
#include "cinn/hlir/pe/schedule.h"
#include "cinn/ir/ir_schedule.h"

DECLARE_bool(cinn_ir_schedule);

namespace cinn {
namespace hlir {

CINNSchedule GetElementwiseScheduleFunc(const std::vector<std::vector<int>>& output_shapes,
                                        const Target& target,
                                        bool vectorizable) {
  return CINNSchedule([=](lang::Args args, lang::RetValue* ret) {
    if (FLAGS_cinn_ir_schedule) {
      CHECK(!args.empty()) << "The input argument of InjectiveSchedule is empty! Please check.\n";
      common::CINNValuePack arg_pack = args[0];
      std::vector<Expr> vec_ast;
      for (int i = 0; i < arg_pack.size(); i++) {
        if (arg_pack[i].is_expr()) {
          Expr temp = arg_pack[i];
          vec_ast.emplace_back(temp);
        }
      }
      CHECK(!vec_ast.empty());
      ir::ModuleExpr mod_expr(vec_ast);
      ir::IRSchedule ir_sch(mod_expr);
      ir_sch.MergeExprs();
      pe::IRElementwiseSchedule(ir_sch, output_shapes.front(), target);
      std::vector<common::CINNValue> res{common::CINNValue(ir_sch.GetModule().GetExprs().at(0))};
      *ret = common::CINNValuePack{res};
    } else {
      CHECK(!args.empty()) << "The input argument of InjectiveSchedule is empty! Please check.\n";
      common::CINNValuePack arg_pack = args[0];
      Expr out                       = arg_pack[0];
      poly::StageMap stages          = arg_pack[1];
      CHECK(out.as_tensor());
      CHECK_EQ(arg_pack.size(), 2UL);
      if (target.arch == Target::Arch::NVGPU) {
        pe::CudaScheduleInjective(stages[out.as_tensor_ref()], output_shapes.front(), target);
      } else if (target.arch == Target::Arch::X86) {
        pe::ScheduleInjectiveCPU(stages[out.as_tensor_ref()], output_shapes.front(), target, vectorizable);
      }
      *ret = arg_pack;
    }
  });
}

CINNSchedule GetInjectiveScheduleFunc(const std::vector<std::vector<int>>& output_shapes,
                                      const Target& target,
                                      bool vectorizable) {
  return CINNSchedule([=](lang::Args args, lang::RetValue* ret) {
    if (FLAGS_cinn_ir_schedule) {
      CHECK(!args.empty()) << "The input argument of InjectiveSchedule is empty! Please check.\n";
      common::CINNValuePack arg_pack = args[0];
      std::vector<Expr> vec_ast;
      for (int i = 0; i < arg_pack.size(); i++) {
        if (arg_pack[i].is_expr()) {
          Expr temp = arg_pack[i];
          vec_ast.emplace_back(temp);
        }
      }
      CHECK(!vec_ast.empty());
      ir::ModuleExpr mod_expr(vec_ast);
      ir::IRSchedule ir_sch(mod_expr);
      ir_sch.MergeExprs();
      pe::IRInjectiveSchedule(ir_sch, output_shapes.front(), target);
      /*if (target.arch == Target::Arch::NVGPU) {
        pe::IRInjectiveSchedule(ir_sch, output_shapes.front(), target);
      } else if (target.arch == Target::Arch::X86) {
        pe::IRScheduleInjectiveCPU(ir_sch, output_shapes.front(), target, vectorizable);
      }*/
      std::vector<common::CINNValue> res{common::CINNValue(ir_sch.GetModule().GetExprs().at(0))};
      *ret = common::CINNValuePack{res};
    } else {
      CHECK(!args.empty()) << "The input argument of InjectiveSchedule is empty! Please check.\n";
      common::CINNValuePack arg_pack = args[0];
      Expr out                       = arg_pack[0];
      poly::StageMap stages          = arg_pack[1];
      CHECK(out.as_tensor());
      CHECK_EQ(arg_pack.size(), 2UL);
      if (target.arch == Target::Arch::NVGPU) {
        pe::CudaScheduleInjective(stages[out.as_tensor_ref()], output_shapes.front(), target);
      } else if (target.arch == Target::Arch::X86) {
        pe::ScheduleInjectiveCPU(stages[out.as_tensor_ref()], output_shapes.front(), target, vectorizable);
      }
      *ret = arg_pack;
    }
  });
}

std::string GetExternFuncName(const common::Target& target, const common::Type& type, const std::string& func_name) {
  std::string target_func_name_type;
  if (target.arch == common::Target::Arch::NVGPU) {
    target_func_name_type.assign("cinn_cuda_");
  } else if (target.arch == common::Target::Arch::X86) {
    target_func_name_type.assign("cinn_host_");
  } else {
    LOG(FATAL) << func_name << "only supports X86 and NVGPU ! Please Check.\n";
  }
  target_func_name_type.append(func_name);
  target_func_name_type.append("_");
  if (type.is_float(16)) {
    target_func_name_type.append("fp16");
  } else if (type.is_float(32)) {
    target_func_name_type.append("fp32");
  } else if (type.is_float(64)) {
    target_func_name_type.append("fp64");
  } else if (type.is_int(32)) {
    target_func_name_type.append("int32");
  } else if (type.is_int(64)) {
    target_func_name_type.append("int64");
  } else {
    LOG(FATAL) << func_name << "only supports fp16, fp32, fp64, int32 and int64 ! Please Check.\n";
  }
  return target_func_name_type;
}

}  // namespace hlir
}  // namespace cinn
