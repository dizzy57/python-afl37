#include <iostream>
#include <string>
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace std;

int tracefunc(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
  frame->f_trace_lines = 0;
  frame->f_trace_opcodes = 1;
  auto code = frame->f_code;
  auto filename = py::reinterpret_borrow<py::str>(code->co_filename);
  auto name = py::reinterpret_borrow<py::str>(code->co_name); // Make this a string_view?
  cout << what << " " << string(filename) << ":" << frame->f_lineno << " " \
       << frame->f_lasti  << " " << string(name) << endl;
  return 0;
}

void init() {
  cout << "0 call 1 exception 2 line 3 return 7 opcode" << endl;
  cout << "0,2 frame push 3 frame pop 1 ignore" << endl << endl;;
  PyEval_SetTrace(tracefunc, nullptr);
}

PYBIND11_MODULE(afl37, m) {
    m.def("init", &init);
}
