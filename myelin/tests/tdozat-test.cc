#include <iostream>
#include <string>

#include "base/init.h"
#include "base/flags.h"
#include "base/logging.h"
#include "myelin/compute.h"
#include "myelin/flow.h"
#include "myelin/graph.h"
#include "myelin/profile.h"
#include "myelin/multi-process.h"
#include "myelin/kernel/tensorflow.h"

DEFINE_string(model, "local/tdozat-step1.flow", "input file with flow model");
DEFINE_int32(repeat, 100, "Number of times test is repeated");
DEFINE_bool(dump_flow, false, "Dump analyzed flow to stdout");
DEFINE_bool(dump_cell, false, "Dump network cell to stdout");
DEFINE_bool(parallel, false, "Run matmuls in parallel");

using namespace sling;
using namespace sling::myelin;

void DummyGather(const TensorData &embeddings, const TensorData &indices,
                       TensorData *lookup) {
}

void DummyDot(const TensorData &a,
              const TensorData &b,
              TensorData *result) {
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Set up kernel library.
  Library library;
  library.Register("Gather", "DummyGather", DummyGather)
     .Input(0, DT_FLOAT, 2)
     .Input(1, DT_INT32, 2)
     .Output(0, DT_FLOAT, 3);
  library.Register("BatchMatMul", "DummyDot", DummyDot)
     .Input(0, DT_FLOAT, 3)
     .Input(1, DT_FLOAT, 3)
     .Output(0, DT_FLOAT, 3);
  RegisterTensorflowLibrary(&library);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_model));

  string prefix = "RNN0_2/RNN/while/time_step/rnn_step/LSTMCell/";
  flow.Var(prefix + "hidden_in/hidden_tm1:0")->data = nullptr;
  flow.Var(prefix + "hidden_in/cell_tm1:0")->data = nullptr;
  flow.Var(prefix + "inputs:0")->data = nullptr;
  flow.Var(prefix + "hidden_t/h_out:0")->out = true;
  flow.Var(prefix + "c_out:0")->out = true;

  flow.Var("strided_slice_11:0")->name = "word1";
  flow.Var("strided_slice_12:0")->name = "word2";
  flow.Var("strided_slice_13:0")->name = "pos";

  flow.Var("recur_out_2:0")->in = true;
  flow.Var("recur_out_2:0")->data = nullptr;
  flow.Var("recur_out_2:0")->size = 0;

  if (FLAGS_parallel) {
    int t = 0;
    for (auto *matmul : flow.Find({"MatMul"})) {
      matmul->task = t++;
    }
  }

  GraphOptions rawopts;
  FlowToDotGraphFile(flow, rawopts, "/tmp/raw-tdozat.dot");

  // Analyze flow.
  flow.Analyze(library);
  DCHECK(flow.IsConsistent());

  if (FLAGS_dump_flow) {
    std::cout << flow.ToString();
  }

  // dot -Granksep=1.5 -Gnodesep=0.3 /tmp/tdozat.dot -Tsvg
  GraphOptions opts;
  FlowToDotGraphFile(flow, opts, "/tmp/tdozat.dot");

  // Compile model.
  Network network;
  MultiProcessorRuntime mprt;
  if (FLAGS_repeat > 0) network.set_profiling(true);
  if (FLAGS_parallel) network.set_runtime(&mprt);
  CHECK(network.Compile(flow, library));

  Cell *lookup = network.GetCell("lookup");
  Cell *lstmfw = network.GetCell("lstmfw");
  Cell *mlps = network.GetCell("mlps");
  CHECK(lookup != nullptr);
  CHECK(lstmfw != nullptr);
  CHECK(mlps != nullptr);
  if (FLAGS_dump_cell) {
    std::cout << lookup->ToString();
    std::cout << lstmfw->ToString();
  }

  // objdump -D -Mintel,x86-64 -bbinary -mi386 --no-show-raw-insn /tmp/tdozat.bin
  lstmfw->WriteCodeToFile("/tmp/tdozat.bin");

  // Test model.
  if (FLAGS_repeat > 0) {
    LOG(INFO) << "Profile model";

    Instance data1(lookup);
    data1.Clear();
    for (int i = 0; i < FLAGS_repeat; ++i) {
      data1.Compute();
    }

    Profile profile1(&data1);
    std::cout << profile1.ASCIIReport() << "\n";

    Instance data2(lstmfw);
    data2.Clear();
    for (int i = 0; i < FLAGS_repeat; ++i) {
      data2.Compute();
    }

    Profile profile2(&data2);
    std::cout << profile2.ASCIIReport() << "\n";

    Instance data3(mlps);
    data3.Clear();
    for (int i = 0; i < FLAGS_repeat; ++i) {
      data3.Compute();
    }

    Profile profile3(&data3);
    std::cout << profile3.ASCIIReport() << "\n";
  }

  return 0;
}