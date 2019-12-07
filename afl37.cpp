#include <pybind11/pybind11.h>
#include <iostream>
#include <string>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;
using namespace std;

int tracefunc(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg) {
  if (LIKELY(what == PyTrace_OPCODE) || (what == PyTrace_EXCEPTION)) {
    // Log both exception path and normal path, they give different coverage
    cout << what << " " << frame->f_lineno << " " << frame->f_lasti << endl;
  } else if ((what == PyTrace_CALL) || (what == PyTrace_LINE)) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
    auto code = frame->f_code;
    auto filename = py::reinterpret_borrow<py::str>(code->co_filename);
    auto name = py::reinterpret_borrow<py::str>(code->co_name);
    // Cast py::str to string_view?
    cout << "push frame " << string(filename) << " " << string(name) << endl;
  } else if (what == PyTrace_RETURN) {
    cout << "pop frame" << endl;
  }
  return 0;
}

void init() {
  PyEval_SetTrace(tracefunc, nullptr);
}

PYBIND11_MODULE(afl37, m) {
  m.def("init", &init);
}
