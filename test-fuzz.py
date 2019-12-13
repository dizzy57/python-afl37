#!/usr/bin/env python
import os
import sys
import time

if len(sys.argv) == 1:
    import afl
else:
    import afl37 as afl

stdin = sys.stdin.buffer


def f(data):
    data += " " * 4
    if data[0] == "x":
        if data[3] == "y":
            return True


def g(data):
    data += " " * 4
    if data[0] == "x" and data[3] == "y":
        return True


while afl.loop(10_000):
    stdin.seek(0)

    try:
        data = stdin.read().decode("ascii")
    except UnicodeDecodeError:
        continue

    if f(data):
        raise RuntimeError

os._exit(0)
