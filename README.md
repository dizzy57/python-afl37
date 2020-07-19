# python-afl37

Bytecode-level coverage for fuzzing python programs with [AFL](https://github.com/google/AFL).

Well-known [`python-afl`](https://github.com/jwilk/python-afl) module allows you to fuzz python targets using line-level code coverage to guide the fuzzing process. Starting with CPython version 3.7 the interpreter is able to trace execution [on the bytecode level](https://docs.python.org/3/whatsnew/3.7.html#other-cpython-implementation-changes), giving AFL a better insight into target.

This module tries to be API-compatible with `python-afl` while maintaining similar or better runtime performance.
