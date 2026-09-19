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


signal_path = Path(sys.argv[1])
runtime_path = Path(sys.argv[2])
output_path = Path(sys.argv[3])

signal = signal_path.read_text(encoding="utf-8").split("#else /* __SWITCH__ */", 1)[0]
runtime = runtime_path.read_text(encoding="utf-8")

prepare = runtime.index("status = runtime_prepare_arm64ec();")
done = runtime.index("if (!status) status = runtime_init_process_done( &suspend );", prepare)
launch_text = (
    "wine_nx_start_arm64ec_thread( (PRTL_THREAD_START_ROUTINE)entry, "
    "teb->Peb, suspend, teb );"
)
launch = runtime.index(launch_text, done)
park = runtime.index("park_forever();", launch)
if not prepare < done < launch < park or launch - prepare > 1200:
    raise RuntimeError("unexpected ARM64EC startup sequence")

function = extract_function(signal, "void DECLSPEC_NORETURN wine_nx_start_arm64ec_thread")
output_path.write_text(function, encoding="utf-8")
