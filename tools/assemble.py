#!/usr/bin/env python3
"""Build src/main.c from its parts (head, game, boss)."""
import re, sys
src_dir = "src/"
out = open(sys.argv[1], "w")
def emit(name):
    for line in open(src_dir + name):
        m = re.match(r"\s*/\*#INCLUDE (\S+)\*/", line)
        if m:
            emit(m.group(1))
        else:
            out.write(line)
emit("main_head.c")
emit("game_part.c")
out.close()
