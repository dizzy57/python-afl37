#include <pybind11/pybind11.h>
#include <bitset>
#include <iostream>
#include <stack>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;
using std::cout, std::endl;

// With all the right shifts employed by hash_combine and previous_location_
// calculations it's better to set this to something large (e.g. not 1)
const int kIsException = 1 << 6;

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

class Tracer {
 public:
  void TraceOpcode(const PyFrameObject* const frame, int is_exception = 0) {
    std::size_t current_location = current_frame_hash_;
    auto last_opcode_index = frame->f_lasti;
    hash_combine(current_location, last_opcode_index, is_exception);
    std::size_t afl_map_offset = current_location ^ previous_location_;
    previous_location_ = current_location >> 1;
    cout << current_frame_hash_ % 100 << " " << std::bitset<16>(afl_map_offset)
         << endl;
  }
  void PushFrame(PyFrameObject* const frame) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
    previous_frame_hashes_.push(current_frame_hash_);
    current_frame_hash_ = HashFrame(frame);
    cout << "push frame " << current_frame_hash_ % 100 << endl;
  }
  void PopFrame() {
    cout << "pop frame " << current_frame_hash_ % 100 << endl;
    current_frame_hash_ = previous_frame_hashes_.top();
    previous_frame_hashes_.pop();
  }
  ~Tracer() { cout << "tracing end" << endl; }

 private:
  std::size_t previous_location_ = 0;
  std::size_t current_frame_hash_ = 0;
  std::stack<std::size_t> previous_frame_hashes_;
};

int TraceFunc(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg) {
  // Having an empty capsule name is a bad idea, but I dont want to have a
  // string comparison on the hot path.
  auto tracer = static_cast<Tracer*>(PyCapsule_GetPointer(obj, nullptr));
  // Log both normal path and exception propagation path, they should give
  // different coverage
  if (LIKELY(what == PyTrace_OPCODE)) {
    tracer->TraceOpcode(frame);
  } else if (what == PyTrace_EXCEPTION) {
    tracer->TraceOpcode(frame, kIsException);
  } else if ((what == PyTrace_CALL) || (what == PyTrace_LINE)) {
    // If we encounter a PyTrace_LINE event, that means that we've arrived in a
    // new frame that was in a middle of execution. Line event always precedes
    // opcode event and opcode event will be raised if we set `f_trace_opcodes`
    // in line event handler. See `maybe_call_line_trace()` in `ceval.c`
    tracer->PushFrame(frame);
  } else if (what == PyTrace_RETURN) {
    tracer->PopFrame();
  }
  return 0;
}

void init() {
  auto tracer = py::capsule(
      new Tracer(), [](void* ptr) { delete static_cast<Tracer*>(ptr); });
  PyEval_SetTrace(TraceFunc, tracer.ptr());
}

PYBIND11_MODULE(afl37, m) {
  m.def("init", &init);
}
