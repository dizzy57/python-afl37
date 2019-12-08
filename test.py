from sys import settrace

def f():
    x = 5
    # 1/0
    return


def h():
  f()
  settrace(None)

import afl37

def g():
  afl37.init()
  x = 1
  return x


g()
x = 11
h()
print("python end")
