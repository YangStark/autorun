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


runtime = Path(sys.argv[1]).read_text(encoding="utf-8")
output = Path(sys.argv[2])
output.write_text(extract_function(runtime, "static NTSTATUS runtime_create_registry_path"), encoding="utf-8")
