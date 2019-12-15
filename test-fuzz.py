#!/usr/bin/env python
import os
import sys

if len(sys.argv) == 1:
    import afl
else:
    import afl37 as afl

stdin = sys.stdin.buffer
X, Y = b"xy"


def easy(data):
    data += b" " * 4
    if data[0] == X:
        if data[3] == Y:
            return True


def hard(data):
    data += b" " * 4
    if data[0] == X and data[3] == Y:
        return True


def rec(n, f, data):
    if n:
        f(data)
        return rec(n - 1, f, data[1:])
    else:
        return f(data)


while afl.loop(10_000):
    stdin.seek(0)
    data = stdin.read()

    if rec(10, hard, data):
        raise RuntimeError

os._exit(0)
