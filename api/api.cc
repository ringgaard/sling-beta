#include <python2.7/Python.h>

#include "api/frames.h"

namespace sling {

// hello.cc
PyObject *helloworld(PyObject *self, PyObject *args);

static PyMethodDef py_funcs[] = {
  {"helloworld", helloworld, METH_NOARGS, "helloworld( ): say hello!!\n"},
  {nullptr, nullptr, 0, nullptr}
};

static void RegisterModule() {
  PyObject *module = Py_InitModule3("sling", sling::py_funcs, "SLING API");
  PyStore::Define(module);
}

}  // namespace sling

extern "C" void initsling() {
  sling::RegisterModule();
}

