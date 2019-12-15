#include <pybind11/pybind11.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stack>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;

using u8 = uint8_t;
using u32 = uint32_t;

// With all the right shifts employed by hash_combine and previous_location_
// calculations it's better to set this to something large (e.g. not 1)
const int kIsException = 1 << 6;

// hash_combine() taken from https://stackoverflow.com/a/38140932

inline void hash_combine(std::size_t& seed[[maybe_unused]]) {}

template <typename T, typename... Rest>
inline void hash_combine(std::size_t& seed, const T& v, Rest... rest) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  hash_combine(seed, rest...);
}

std::string_view PyStringView(PyObject* const obj) {
  if (PyUnicode_READY(obj) != 0) {
    _exit(1);
  }
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

// forward
int TraceFunc(PyObject*, PyFrameObject*, int, PyObject*);

//#define DISABLE_FRAME_STACK
class Tracer {
 public:
  void TraceOpcode(const PyFrameObject* const frame, int is_exception = 0) {
#ifndef DISABLE_FRAME_STACK
    std::size_t current_location = current_frame_hash_;
#else
    std::size_t current_location = HashFrame(frame);
#endif
    auto last_opcode_index = frame->f_lasti;
    hash_combine(current_location, last_opcode_index, is_exception);
    std::size_t afl_map_offset = current_location ^ previous_location_;
    ++afl_area_ptr_[afl_map_offset % kMapSize];
    previous_location_ = current_location >> 1;
  }
  void PushFrame(PyFrameObject* const frame) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
#ifndef DISABLE_FRAME_STACK
    previous_frame_hashes_.push(current_frame_hash_);
    current_frame_hash_ = HashFrame(frame);
#endif
  }
  void PopFrame() {
#ifndef DISABLE_FRAME_STACK
    current_frame_hash_ = previous_frame_hashes_.top();
    previous_frame_hashes_.pop();
#endif
  }
  void ResetState() { previous_location_ = 0; }
  void MapSharedMemory() {
    char* env_var_value = std::getenv(kShmEnvVar);

    if (!env_var_value) {
      return;
    }
    auto shm_id = std::atoi(env_var_value);
    auto shmat_res = shmat(shm_id, nullptr, 0);
    if (shmat_res == reinterpret_cast<void*>(-1)) {
      _exit(1);
    }
    afl_area_ptr_ = static_cast<u8*>(shmat_res);
  }
  void StartTracing() const {
    if (afl_area_ptr_) {
      PyEval_SetTrace(TraceFunc, nullptr);
    }
  }
  void StopTracing() {
    afl_area_ptr_ = nullptr;
    PyEval_SetTrace(nullptr, nullptr);
  }

 private:
  u8* afl_area_ptr_ = nullptr;
  std::size_t previous_location_ = 0;
#ifndef DISABLE_STACK
  std::size_t current_frame_hash_ = 0;
  std::stack<std::size_t> previous_frame_hashes_;
#endif

  static const u8 kMapSizePow2 = 16;
  static const std::size_t kMapSize = 1 << kMapSizePow2;
  static constexpr const char* kShmEnvVar = "__AFL_SHM_ID";
} tracer;

[[gnu::hot]] int TraceFunc(PyObject*,
                           PyFrameObject* frame,
                           int what,
                           PyObject*) {
  // Log both normal path and exception propagation path, they should give
  // different coverage
  if (LIKELY(what == PyTrace_OPCODE)) {
    tracer.TraceOpcode(frame);
  } else if (what == PyTrace_EXCEPTION) {
    tracer.TraceOpcode(frame, kIsException);
  } else if ((what == PyTrace_CALL) || (what == PyTrace_LINE)) {
    // If we encounter a PyTrace_LINE event, that means that we've arrived in a
    // new frame that was in a middle of execution. Line event always precedes
    // opcode event and opcode event will be raised if we set `f_trace_opcodes`
    // in line event handler. See `maybe_call_line_trace()` in `ceval.c`
    tracer.PushFrame(frame);
  } else if (what == PyTrace_RETURN) {
    tracer.PopFrame();
  }
  return 0;
}

class ForkServer {
 public:
  static void Start() {
    // Report our presense to controlling process
    if (!WriteStatus(0)) {
      // We don't have a controlling process, continue normal execution
      return;
    }

    SetExceptHook();

    bool is_persistent = true;
    bool child_stopped = false;
    pid_t child_pid;

    // TODO: save and reset sigchld with PySys_*

    while (true) {
      u32 was_killed = ReadControlOrDie();

      if (child_stopped && was_killed) {
        child_stopped = false;
        WaitPidOrDie(child_pid);  // Reap
      }

      if (!child_stopped) {
        // Spawn new child process
        child_pid = fork();
        if (child_pid < 0) {
          _exit(1);
        }
        if (!child_pid) {
          // We are in child process, exit from the loop
          return PrepareChild();
        }
      } else {
        // Resume existing child process
        kill(child_pid, SIGCONT);
        child_stopped = false;
      }

      WriteStatusOrDie(child_pid);
      int status = WaitPidOrDie(child_pid, is_persistent ? WUNTRACED : 0);
      child_stopped = WIFSTOPPED(status);
      WriteStatusOrDie(status);
    }
  }

 private:
  static const int kControlFd = 198;
  static const int kStatusFd = kControlFd + 1;

  static u32 ReadControlOrDie() {
    u32 data;
    if (read(kControlFd, &data, 4) != 4) {
      _exit(1);
    }
    return data;
  }

  static bool WriteStatus(const u32 data) {
    return write(kStatusFd, &data, 4) == 4;
  }

  static void WriteStatusOrDie(const u32 data) {
    if (!WriteStatus(data)) {
      _exit(1);
    }
  }

  static int WaitPidOrDie(const pid_t pid, int options = 0) {
    int status;
    if (waitpid(pid, &status, options) < 0) {
      _exit(1);
    }
    return status;
  }

  static void SetExceptHook() {
    auto sys = py::module::import("sys");
    sys.attr("excepthook") = py::cpp_function(
        [](py::object, py::object, py::object) { raise(SIGUSR1); });
  }

  static void PrepareChild() {
    close(kControlFd);
    close(kStatusFd);
    // TODO: restore sighandler for sigchld

    return;
  }
};

bool loop(long max_cnt) {
  static u32 cur_cnt = 0;
  tracer.ResetState();

  if (cur_cnt == 0) {
    tracer.MapSharedMemory();
    ForkServer::Start();  // child returns here
    // is_persistent? - return it from forkserver
    cur_cnt = 1;
    tracer.StartTracing();
    return true;
  }

  bool cont = (max_cnt > 0) && (cur_cnt < max_cnt);

  if (cont) {
    raise(SIGSTOP);
    ++cur_cnt;
    return true;
  } else {
    // Disable tracing: collected coverage will be reported on process exit
    tracer.StopTracing();
    // maybe _exit(0); ?
    return false;
  }
}

PYBIND11_MODULE(afl37, m) {
  m.def("loop", &loop, py::arg("max_cnt") = 0);
}
