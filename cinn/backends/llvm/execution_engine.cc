#include "cinn/backends/llvm/execution_engine.h"

#include <llvm/ADT/Triple.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/InitializePasses.h>
#include <llvm/PassRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/NewGVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

#include <cmath>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <string_view>
#include <utility>

#include "cinn/backends/llvm/cinn_runtime_llvm_ir.h"
#include "cinn/backends/llvm/codegen_llvm.h"
#include "cinn/backends/llvm/llvm_optimizer.h"
#include "cinn/backends/llvm/llvm_util.h"
#include "cinn/backends/llvm/runtime_symbol_registry.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/runtime/intrinsic.h"

namespace cinn::backends {
namespace {
void InitializeLLVMPasses() {
  static std::mutex mu;
  std::lock_guard<std::mutex> lock(mu);

  auto &registry = *llvm::PassRegistry::getPassRegistry();
  llvm::initializeCore(registry);
  llvm::initializeTransformUtils(registry);
  llvm::initializeScalarOpts(registry);
  llvm::initializeIPO(registry);
  llvm::initializeInstCombine(registry);
  llvm::initializeAggressiveInstCombine(registry);
  llvm::initializeAnalysis(registry);
  llvm::initializeVectorization(registry);

  // llvm::initializeCodeGen(registry);
  // llvm::initializeTarget(registry);
  // llvm::initializeCodeGenPreparePass(registry);
}
}  // namespace
void NaiveObjectCache::notifyObjectCompiled(const llvm::Module *m, llvm::MemoryBufferRef obj_buffer) {
  cached_objects_[m->getModuleIdentifier()] =
      llvm::MemoryBuffer::getMemBufferCopy(obj_buffer.getBuffer(), obj_buffer.getBufferIdentifier());
}

std::unique_ptr<llvm::MemoryBuffer> NaiveObjectCache::getObject(const llvm::Module *m) {
  auto it = cached_objects_.find(m->getModuleIdentifier());
  if (it == cached_objects_.end()) {
    LOG(INFO) << "No object for " << m->getModuleIdentifier() << " in cache. Compiling.";
    return nullptr;
  }

  LOG(INFO) << "Object for " << m->getModuleIdentifier() << " loaded from cache.";
  return llvm::MemoryBuffer::getMemBuffer(it->second->getMemBufferRef());
}

/*static*/ std::unique_ptr<ExecutionEngine> ExecutionEngine::Create(const ExecutionOptions &config) {
  LOG(INFO) << "===================== Create CINN ExecutionEngine begin ====================";
  LOG(INFO) << "initialize llvm config";
  LOG(INFO) << "llvm version: " << LLVM_VERSION_STRING;
  LOG(INFO) << "llvm default target triple: " << LLVM_DEFAULT_TARGET_TRIPLE;
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  InitializeLLVMPasses();

  auto engine = std::make_unique<ExecutionEngine>(/*enable_object_cache=*/true);

  auto compile_layer_creator = [&engine](llvm::orc::JITTargetMachineBuilder jtmb)
      -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
    auto machine = jtmb.createTargetMachine();
    LOG(INFO) << "create llvm compile layer";
    LOG(INFO) << "Target Name: " << (*machine)->getTarget().getName();
    LOG(INFO) << "Target CPU: " << (*machine)->getTargetCPU().str() << std::endl;
    if (!machine) return machine.takeError();
    return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(std::move(*machine), engine->cache_.get());
  };

  auto object_layer_creator = [&](llvm::orc::ExecutionSession &session, const llvm::Triple &triple) {
    auto object_layer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(
        session, []() { return std::make_unique<llvm::SectionMemoryManager>(); });
    llvm::orc::JITDylib *main_jd = session.getJITDylibByName("<main>");
    if (!main_jd) {
      main_jd = &session.createJITDylib("<main>");
    }
    return object_layer;
  };

  LOG(INFO) << "create jit execution engine";
  engine->jit_ = llvm::cantFail(llvm::orc::LLJITBuilder()
                                    .setCompileFunctionCreator(compile_layer_creator)
                                    .setObjectLinkingLayerCreator(object_layer_creator)
                                    .create());
  engine->jit_->getMainJITDylib().addGenerator(llvm::cantFail(
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(engine->jit_->getDataLayout().getGlobalPrefix())));

  LOG(INFO) << "register runtime call symbols";

  engine->RegisterRuntimeSymbols();

  LOG(INFO) << "===================== Create CINN ExecutionEngine end ====================";
  return engine;
}

void ExecutionEngine::Link(const lang::Module &module) {
  llvm::SMDiagnostic error;
  auto ctx     = std::make_unique<llvm::LLVMContext>();
  auto m       = llvm::parseAssemblyString(AsStringRef(backends::kRuntimeLlvmIr), error, *ctx);
  auto b       = std::make_unique<llvm::IRBuilder<>>(*ctx);
  auto emitter = std::make_unique<CodeGenLLVM>(m.get(), b.get());

  for (auto &buffer : module->buffers) {
    auto expr = runtime::BufferCreate(buffer.as_buffer_ref());
    emitter->Visit(&expr);
  }

  for (auto &function : module.functions()) {
    ir::Expr expr(function);
    LOG(INFO) << "cinn:\n" << expr;
    auto *f = emitter->Visit(&expr);
    LOG(INFO) << "fn:\n" << DumpToString(*f);
    CHECK(!llvm::verifyModule(*m, &llvm::errs())) << "Invalid module detected";
  }

  CHECK(!llvm::verifyModule(*m, &llvm::errs())) << "Invalid module found";

  LLVMModuleOptimizer optimize(nullptr, 3, {});
  optimize(m.get());
  CHECK(!llvm::verifyModule(*m, &llvm::errs())) << "Invalid optimized module detected";

  CHECK(AddModule(std::move(m), std::move(ctx)));

  decltype(auto) es = jit_->getExecutionSession();
  {
    VLOG(1) << "======= dump jit execution session ======";
    std::string buffer;
    llvm::raw_string_ostream os(buffer);
    es.dump(os);
    os.flush();
    VLOG(1) << buffer;
  }
}

bool ExecutionEngine::AddModule(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context) {
  {
    LOG(INFO) << "======= dump jit lib ==========";
    std::string buffer;
    llvm::raw_string_ostream os(buffer);
    module->print(os, {});
    // main_jd_->dump(os);
    os.flush();
    LOG(INFO) << buffer;
  }
  llvm::orc::ThreadSafeContext tsc(std::move(context));
  llvm::orc::ThreadSafeModule tsm(std::move(module), std::move(tsc));
  if (auto error = jit_->addIRModule(std::move(tsm))) {
    LOG(INFO) << "LLVM link module error!";
    return false;
  }
  return true;
}

void *ExecutionEngine::Lookup(std::string_view name) {
  std::lock_guard<std::mutex> lock(mu_);
  LOG(INFO) << "======= Lookup symbol[" << std::string(AsStringRef(name)) << "]";

  if (auto symbol = jit_->lookup(AsStringRef(name))) {
    return reinterpret_cast<void *>(symbol->getAddress());
  }

  LOG(ERROR) << "Unknown symbol name[" << name << "]";
  return nullptr;
}

void ExecutionEngine::RegisterRuntimeSymbols() {
  const auto &registry = RuntimeSymbolRegistry::Global();
  auto *session        = &jit_->getExecutionSession();
  for (const auto &[name, addr] : registry.All()) {
    llvm::cantFail(jit_->defineAbsolute(name, {llvm::pointerToJITTargetAddress(addr), llvm::JITSymbolFlags::None}));
  }
}

}  // namespace cinn::backends
