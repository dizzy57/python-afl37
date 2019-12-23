#include <pybind11/pybind11.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define LIKELY(condition) __builtin_expect(static_cast<bool>(condition), 1)

namespace py = pybind11;

using u8 = uint8_t;
using u32 = uint32_t;

// forward
int TraceFunc(PyObject*, PyFrameObject*, int, PyObject*);

class Tracer {
 public:
  void TraceOpcode(const PyFrameObject* const frame, u32 is_exception = 0) {
    u32 current_location =
        current_frame_hash_ ^ HashOpcode(frame, is_exception);
    u32 afl_map_offset = current_location ^ previous_location_;
    ++afl_area_ptr_[afl_map_offset % kMapSize];
    previous_location_ = current_location >> 1;
  }

  void PushFrame(PyFrameObject* const frame) {
    frame->f_trace_lines = 0;
    frame->f_trace_opcodes = 1;
    current_frame_hash_ = HashFrame(frame);
  }

  void PopFrame(const PyFrameObject* const frame) {
    current_frame_hash_ = HashFrame(frame->f_back);
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
  u32 previous_location_ = 0;
  u32 current_frame_hash_ = 0;

  static constexpr u8 kMapSizePow2 = 16;
  static constexpr u32 kMapSize = 1 << kMapSizePow2;
  static constexpr const char* kShmEnvVar = "__AFL_SHM_ID";

  static u32 FNV_1a(const std::string_view&& s) {
    u32 hash = UINT32_C(0x811c9dc5);
    for (u8 c : s) {
      hash ^= c;
      hash *= UINT32_C(0x01000193);
    }
    return hash;
  }

  static constexpr u32 Hash_u32(u32 x) {
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
    return x;
  }

  static std::string_view PyStringView(PyObject* const obj) {
    if (PyUnicode_READY(obj) != 0) {
      _exit(1);
    }
    std::size_t size = PyUnicode_GET_LENGTH(obj) * PyUnicode_KIND(obj);
    return {static_cast<char*>(PyUnicode_DATA(obj)), size};
  }

  static u32 HashFrame(const PyFrameObject* const frame) {
    auto code = frame->f_code;
    u32 file_name = FNV_1a(PyStringView(code->co_filename));
    u32 code_object_name = FNV_1a(PyStringView(code->co_name));
    u32 first_lineno = Hash_u32(code->co_firstlineno);
    return file_name ^ code_object_name ^ first_lineno;
  }

  static u32 HashOpcode(const PyFrameObject* const frame, u32 is_exception) {
    u32 last_opcode_index = frame->f_lasti;
    // Opcode indices are even, is_exception is either 0 or 1
    return Hash_u32(last_opcode_index ^ is_exception);
  }
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
    tracer.TraceOpcode(frame, /* is_exception */ 1);
  } else if ((what == PyTrace_CALL) || (what == PyTrace_LINE)) {
    // If we encounter a PyTrace_LINE event, that means that we've arrived in a
    // new frame that was in a middle of execution. Line event always precedes
    // opcode event and opcode event will be raised if we set `f_trace_opcodes`
    // in line event handler. See `maybe_call_line_trace()` in `ceval.c`
    tracer.PushFrame(frame);
  } else if (what == PyTrace_RETURN) {
    tracer.PopFrame(frame);
  }
  return 0;
}

class ForkServer {
 public:
  static bool ShouldBePersistent() {
    return static_cast<bool>(std::getenv(kPersistentEnvVar));
  }

  static void Start(bool is_persistent) {
    // Report our presense to the controlling process
    if (!WriteStatus(0)) {
      // We don't have a controlling process, continue normal execution
      return;
    }

    bool child_stopped = false;
    pid_t child_pid;

    // TODO: save and reset sigchld with PySys_*

    while (true) {
      u32 was_killed = ReadControlOrDie();

      if (child_stopped && was_killed) {
        WaitPidOrDie(child_pid);  // Reap
        child_stopped = false;
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
  static constexpr int kControlFd = 198;
  static constexpr int kStatusFd = kControlFd + 1;
  static constexpr const char* kPersistentEnvVar = "PYTHON_AFL_PERSISTENT";

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

  static void PrepareChild() {
    close(kControlFd);
    close(kStatusFd);
    // TODO: restore sighandler for sigchld

    return;
  }
};

void SetPythonExceptHook() {
  auto sys = py::module::import("sys");
  sys.attr("excepthook") = py::cpp_function(
      [](py::object, py::object, py::object) { raise(SIGUSR1); });
}

bool loop(u32 max_cnt) {
  static u32 cur_cnt = 0;
  static bool is_persistent = false;

  tracer.ResetState();

  if (cur_cnt == 0) {
    cur_cnt = 1;
    is_persistent = ForkServer::ShouldBePersistent();

    SetPythonExceptHook();
    tracer.MapSharedMemory();
    tracer.StartTracing();
    ForkServer::Start(is_persistent);  // child returns here
    return true;
  }

  bool cont = is_persistent && ((max_cnt == 0) || (cur_cnt < max_cnt));

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
  m.def("init", []() { loop(1); });
  m.def("loop", &loop, py::arg("max_cnt") = 0);
}
