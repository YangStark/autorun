#!/usr/bin/env python3
"""Check architecture and direct dependency closure of the WoW64 test package."""
from pathlib import Path
import hashlib
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
stage = root / "wine-nx-probe/build-switch-wow64/sd-card/switch/wine"
readobj = root / "wine-nx-probe/toolchains/llvm-mingw-20260505-ucrt-macos-universal/bin/llvm-readobj"

def inspect(path, option):
    return subprocess.check_output([str(readobj), option, str(path)], text=True)

for directory, arch in (("system32", "aarch64"), ("syswow64", "i386")):
    path = stage / "drive_c/windows" / directory
    modules = {p.name.lower(): p for p in path.glob("*.dll")}
    assert modules, f"No modules in {path}"
    for name, module in modules.items():
        info = inspect(module, "--coff-imports")
        assert f"Arch: {arch}\n" in info, f"Wrong architecture: {module}"
        # Load-time imports only; delay-loaded DLLs are resolved on first use.
        deps = re.findall(r"^Import \{\n  Name: (.+)$", info, re.M)
        missing = [dep for dep in deps if dep.lower() not in modules]
        assert not missing, f"Missing load-time dependency for {module}: {missing}"

wow64 = stage / "drive_c/windows/system32/wow64.dll"
assert "Name: __wine_switch_cpu_backend" in inspect(wow64, "--coff-exports"), "Missing Switch CPU selection export"
ntdll_exports = inspect(stage / "drive_c/windows/system32/ntdll.dll", "--coff-exports")
for hook in ("pWow64SuspendLocalThread", "pWow64PrepareForException"):
    # The bootstrap bypasses init_wow64() and fills these in the PE ntdll.
    assert f"Name: {hook}\n" in ntdll_exports, f"ntdll.dll lacks the WoW64 bootstrap hook {hook}"
cpu = stage / "drive_c/windows/system32/winebox64.dll"
exports = set(re.findall(r"^  Name: (.+)$", inspect(cpu, "--coff-exports"), re.M))
required = {"BTCpuProcessInit", "BTCpuThreadInit", "BTCpuGetBopCode", "BTCpuSimulate",
            "BTCpuGetContext", "BTCpuSetContext", "BTCpuResetToConsistentState",
            "BTCpuSuspendLocalThread", "BTCpuIsProcessorFeaturePresent", "BTCpuUpdateProcessorInformation",
            "__wine_get_unix_opcode"}
assert required <= exports, f"Missing CPU exports: {required - exports}"
smoke = stage / "drive_c/pe32-smoke.exe"
info = inspect(smoke, "--coff-imports")
assert "Arch: i386\n" in info and "Symbol: NtQuerySystemTime" in info and "Symbol: NtTerminateProcess" in info
assert "Type: HIGHLOW" in inspect(smoke, "--coff-basereloc"), "Smoke must be relocatable"
functional = stage / "drive_c/pe32-functional.exe"
info = inspect(functional, "--coff-imports")
assert "Arch: i386\n" in info
for symbol in ("CreateFileW", "ReadFile", "WriteFile", "HeapAlloc", "HeapReAlloc", "TlsAlloc", "TlsSetValue", "TlsGetValue", "NtDisplayString", "NtTerminateProcess"):
    assert f"Symbol: {symbol} " in info, f"Missing functional test import: {symbol}"
assert "Type: HIGHLOW" in inspect(functional, "--coff-basereloc")
assert all(name.lower() in {"kernel32.dll", "ntdll.dll"} for name in re.findall(r"^  Name: (.+)$", info, re.M))
thread_info = inspect(stage / "drive_c/pe32-threads.exe", "--coff-imports")
assert "Arch: i386\n" in thread_info
for symbol in ("CreateThread", "CreateEventW", "SetEvent", "WaitForSingleObject", "CreateMutexW", "ReleaseMutex", "TlsGetValue"):
    assert f"Symbol: {symbol} " in thread_info, f"Missing thread test import: {symbol}"
assert "Type: HIGHLOW" in inspect(stage / "drive_c/pe32-threads.exe", "--coff-basereloc")
lifecycle = stage / "drive_c/pe32-lifecycle.exe"
lifecycle_info = inspect(lifecycle, "--coff-imports")
assert "Arch: i386\n" in lifecycle_info
for symbol in ("CreateThread", "ExitThread", "GetExitCodeThread", "WaitForMultipleObjects", "OpenThread",
               "ResumeThread", "SuspendThread", "InitOnceExecuteOnce", "SleepConditionVariableSRW",
               "EnterCriticalSection", "CreateSemaphoreW", "DuplicateHandle", "GetThreadTimes"):
    assert f"Symbol: {symbol} " in lifecycle_info, f"Missing lifecycle test import: {symbol}"
assert all(name.lower() in {"kernel32.dll", "ntdll.dll"}
           for name in re.findall(r"^  Name: (.+)$", lifecycle_info, re.M))
assert "Type: HIGHLOW" in inspect(lifecycle, "--coff-basereloc")
tls = inspect(lifecycle, "--coff-tls-directory")
assert "AddressOfCallBacks: 0x0" not in tls and "StartAddressOfRawData" in tls, "Lifecycle test needs static TLS"
sevenzip = stage / "drive_c/7zr.exe"
info = inspect(sevenzip, "--coff-imports")
assert "Arch: i386\n" in info and "Type: HIGHLOW" in inspect(sevenzip, "--coff-basereloc"), "7zr must be relocatable"
deps = re.findall(r"^Import \{\n  Name: (.+)$", info, re.M)
syswow64 = {p.name.lower() for p in (stage / "drive_c/windows/syswow64").glob("*.dll")}
assert all(dep.lower() in syswow64 for dep in deps), f"7zr load-time imports not staged: {deps}"
assert (stage / "wine-nx-runtime.nro").read_bytes()[16:20] == b"NRO0"
assert (stage / "target.txt").read_text().strip() == "sdmc:/switch/wine/drive_c/7zr.exe"
assert (stage / "args.txt").read_text().strip().lower() == "c:\\7zr.exe b 1 -mmt2 -md18"
assert (stage / "drive_c/7zr-rename.7z").read_bytes() == (stage / "drive_c/7zr-tree.7z").read_bytes(), \
    "The rename run starts from a copy of the tree archive"
assert not (stage / "drive_c/7zr-rename.7z.tmp").exists()
assert not (stage / "drive_c/no-such-archive.7z").exists(), "The error-path command needs a missing archive"
assert hashlib.sha256((stage / "drive_c/7zr-tree.7z").read_bytes()).hexdigest() == \
    "e477719f40d14d1d34127a14b3e9b657c596032ca3d6bd99dd3f3fc051bc8524", "Staged tree archive differs from the sample"
assert (stage / "drive_c/7zr-sample.7z").read_bytes()[:6] == b"7z\xbc\xaf\x27\x1c"
# The folder tree "7zr a" archives (and "7zr x" restores): the generator's files, byte for byte.
sys.path.insert(0, str(root / "wine-nx-probe/tools"))
tree_module = __import__("make-7zr-tree")
tree_root = stage / "drive_c/7zr-tree"
staged = sorted(p.relative_to(stage / "drive_c").as_posix() for p in tree_root.rglob("*") if p.is_file())
assert staged == sorted(name for name, _, _ in tree_module.TREE), f"Unexpected 7zr tree: {staged}"
assert sorted(p.name for p in tree_root.rglob("*") if p.is_dir()) == ["data", "text"]
for name, data, _ in tree_module.TREE:
    assert (stage / "drive_c" / name).read_bytes() == data, f"7zr tree file differs: {name}"
assert not (stage / "drive_c/wine-nx-tree.7z").exists(), "Staging an archive would replace the one 7zr made on the Switch"
assert not (stage / "drive_c/7zr-out").exists(), "7zr x must create its output folders itself"
assert (stage / "run-entry.txt").read_text().strip() == "1"
assert (stage / "share/wine/nls/locale.nls").exists()
print("WoW64 package: architectures, dependency closure, CPU exports, relocatable PE32, NRO and launch files passed")
