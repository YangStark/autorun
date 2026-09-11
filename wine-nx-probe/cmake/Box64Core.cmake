# Box64 v0.4.0 execution core only. No ELF loader, Linux syscalls, or wrappers.
function(wine_nx_add_box64_core target)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/box64")
    execute_process(COMMAND git -C "${root}" rev-parse HEAD
        OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT revision STREQUAL "dae0917c47b4edd8956f314210417a20fd225c4b")
        message(FATAL_ERROR "Run wine-nx-probe/tools/bootstrap-box64-core.sh first")
    endif()
    execute_process(COMMAND git -C "${root}" status --porcelain
        OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT dirty STREQUAL "")
        message(FATAL_ERROR "Box64 vendor checkout must be clean; refusing unreviewed source changes")
    endif()
    set(sources
        x64run0f.c x64run66.c x64run660f.c x64run66f20f.c x64run66f30f.c
        x64run66d9.c x64run66dd.c x64run66f0.c x64rund8.c x64rund9.c
        x64runda.c x64rundb.c x64rundc.c x64rundd.c x64runde.c x64rundf.c
        x64runf0.c x64runf20f.c x64runf30f.c x64runavx.c x64runavx0f.c
        x64runavx0f38.c x64runavx660f.c x64runavxf20f.c x64runavxf30f.c
        x64runavx660f38.c x64runavx660f3a.c x64runavxf20f38.c
        x64runavxf30f38.c x64runavxf20f3a.c x64runavxf30f3a.c
        x64run_private.c x64primop.c x87emu_private.c x64compstrings.c
        x64shaext.c x64emu.c)
    list(TRANSFORM sources PREPEND "${root}/src/emu/")

    # Keep the vendored revision untouched. The only interpreter change is a
    # before-fetch hook; assert its insertion point against the pinned source.
    file(READ "${root}/src/emu/x64run.c" run_source)
    set(anchor "    while(1) \n#endif\n    {")
    string(FIND "${run_source}" "${anchor}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Pinned Box64 interpreter hook anchor changed")
    endif()
    string(REPLACE "${anchor}" "${anchor}\n        if (wine_nx_box64_before_instruction(emu, addr)) return 0;" run_source "${run_source}")
    string(REPLACE "#include \"modrm.h\"" "#include \"modrm.h\"\nextern int wine_nx_box64_before_instruction(x64emu_t *, uintptr_t);" run_source "${run_source}")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}-x64run.c")
    file(WRITE "${generated}" "${run_source}")
    add_library(${target} STATIC ${sources} "${generated}")
    target_include_directories(${target} SYSTEM PUBLIC "${root}/src/include" "${root}/src"
        "${root}/src/emu" "${root}/src/wrapped/generated")
    target_compile_definitions(${target} PUBLIC ARM64 CONFIG_64BIT STATICBUILD)
    # Track interpreter atomic locks so a guest memory fault can unwind without
    # leaving the native mutex locked. The adapter itself uses real pthreads.
    target_compile_definitions(${target} PRIVATE
        pthread_mutex_lock=wine_nx_box64_mutex_lock
        pthread_mutex_unlock=wine_nx_box64_mutex_unlock)
    target_compile_options(${target} PUBLIC -ffixed-x18)
    if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
        target_include_directories(${target} SYSTEM PUBLIC
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../switch-shims"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../box64-shims")
    endif()
    target_compile_options(${target} PRIVATE -O1 -ffunction-sections -fdata-sections
        -Wno-unused-result)
endfunction()
