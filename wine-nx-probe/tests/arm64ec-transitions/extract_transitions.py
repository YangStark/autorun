#!/usr/bin/env python3
import ast
import re
import sys
from pathlib import Path

names = (
    "DispatchJump",
    "RetToEntryThunk",
    "winebox64ec_check_target",
    "ExitToX64",
    "winebox64ec_enter_live",
    "BeginSimulation",
    "winebox64ec_leave_simulation",
    "winebox64ec_bridge_ec",
)

source = Path(sys.argv[1]).read_text(encoding="utf-8")
output = [".text\n.balign 16\n"]

for name in names:
    start = source.index(f"__ASM_GLOBAL_FUNC( {name},")
    end = source.index(" )", start) + 2
    block = source[start:end]
    strings = re.findall(r'"(?:\\.|[^"\\])*"', block)
    code = "".join(ast.literal_eval(item) for item in strings)
    code = code.replace(".seh_endprologue\n\t", "")
    code = code.replace('"#winebox64ec_run_context_returning"',
                        "winebox64ec_run_context_returning")
    output.append(f".global {name}\n.type {name}, %function\n{name}:\n{code}\n")

output.append('.section .note.GNU-stack,"",%progbits\n')
Path(sys.argv[2]).write_text("".join(output), encoding="utf-8")
