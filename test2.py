from sys import settrace
from afl37 import init

def f():
  yield 1
  yield 2
  yield 3

x = f()
next(x)

def g():
  next(x)
  next(x)

init()

g()

print("python end")
