#!/usr/bin/env python3
import sys
from pathlib import Path


def extract_function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if not depth:
                return source[start:pos + 1] + "\n"
    raise RuntimeError(f"unterminated function: {signature}")


source = Path(sys.argv[1]).read_text(encoding="utf-8")
thread_init = extract_function(source, "NTSTATUS WINAPI ThreadInit")
already_initialized = thread_init.index("if (area->EmulatorData[0]) return STATUS_SUCCESS;")
initialize = thread_init.index("initialize_thread_context( area->ContextAmd64 );")
unix_init = thread_init.index("WINE_UNIX_CALL( winebox64ec_thread_init, &params )")
publish = thread_init.index("area->EmulatorData[0] =")
if not already_initialized < initialize < unix_init < publish:
    raise RuntimeError("unexpected ThreadInit initialization order")

function = extract_function(source, "static void initialize_thread_context")
Path(sys.argv[2]).write_text(function, encoding="utf-8")
