#include <pybind11/pybind11.h>
#include <iostream>
#include <string>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;
using namespace std;

class TraceState {
 public:
  void opcode(PyFrameObject* frame, int is_exception) {
    cout << is_exception << " " << ((size_t)frame) % 100 << " "
         << frame->f_lineno << " " << frame->f_lasti << endl;
  }
  void frame_push(PyFrameObject* frame) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
    auto code = frame->f_code;
    auto filename = py::reinterpret_borrow<py::str>(code->co_filename);
    auto name = py::reinterpret_borrow<py::str>(code->co_name);
    // also use first line of code, as code object names and file names are not
    // sufficient
    //
    // Cast py::str to string_view?
    cout << "push frame " << string(filename) << " " << string(name) << endl;
  }
  void frame_pop() { cout << "pop frame" << endl; }
  ~TraceState() { cout << "tracing end" << endl; }

 private:
};

int tracefunc(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg) {
  auto ts = static_cast<TraceState*>(PyCapsule_GetPointer(obj, nullptr));
  if (LIKELY(what == PyTrace_OPCODE) || (what == PyTrace_EXCEPTION)) {
    // Log both normal path and exception propagation path, they should give
    // different coverage
    ts->opcode(frame, what == PyTrace_EXCEPTION ? 1 : 0);
  } else if ((what == PyTrace_CALL) || (what == PyTrace_LINE)) {
    // If we encounter a PyTrace_LINE event, that means that we've arrived in a
    // new frame that was in a middle of execution. Line event always precedes
    // opcode event and opcode event will be raised if we set `f_trace_opcodes`
    // in line event handler. See `maybe_call_line_trace()` in `ceval.c`
    ts->frame_push(frame);
  } else if (what == PyTrace_RETURN) {
    ts->frame_pop();
  }
  return 0;
}

void init() {
  auto capsule = py::capsule(new TraceState(), [](void* ptr) {
    delete static_cast<TraceState*>(ptr);
  });
  PyEval_SetTrace(tracefunc, capsule.ptr());
}

PYBIND11_MODULE(afl37, m) {
  m.def("init", &init);
}
