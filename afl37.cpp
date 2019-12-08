#include <pybind11/pybind11.h>
#include <iostream>
#include <stack>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;
using std::cout, std::endl;

// hash_combine() taken from https://stackoverflow.com/a/38140932

inline void hash_combine(std::size_t& seed) {}

template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  hash_combine(seed, rest...);
}

std::string_view PyStringView(PyObject* const obj) {
  auto string_ready = PyUnicode_READY(obj);
  assert(string_ready == 0);
  std::size_t size = PyUnicode_GET_LENGTH(obj) * PyUnicode_KIND(obj);
  return {static_cast<char*>(PyUnicode_DATA(obj)), size};
}

std::size_t HashFrame(const PyFrameObject* const frame) {
  auto code = frame->f_code;
  auto file_name = PyStringView(code->co_filename);
  auto code_object_name = PyStringView(code->co_name);

  std::size_t res = code->co_firstlineno;
  hash_combine(res, file_name, code_object_name);
  return res;
}

class TraceState {
 public:
  void opcode(const PyFrameObject* const frame, int is_exception) {
    cout << is_exception << " " << current_hash_ % 100 << " " << frame->f_lasti
         << endl;
  }
  void frame_push(PyFrameObject* const frame) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
    previous_hashes_.push(current_hash_);
    current_hash_ = HashFrame(frame);
    cout << "push frame " << current_hash_ % 100 << endl;
  }
  void frame_pop() {
    cout << "pop frame " << current_hash_ % 100 << endl;
    current_hash_ = previous_hashes_.top();
    previous_hashes_.pop();
  }
  ~TraceState() { cout << "tracing end" << endl; }

 private:
  std::size_t current_hash_ = 0;
  std::stack<std::size_t> previous_hashes_;
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
