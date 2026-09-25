#pragma once
#include <switch.h>
#define RUNTIME_ID "autorun-games/wa2-full"
#define RUNTIME_ROOT "/switch/autorun-games/wa2-full"
#define RUNTIME_NRO "sdmc:" RUNTIME_ROOT "/wine-nx-runtime.nro"
#define RUNTIME_EXE "sdmc:" RUNTIME_ROOT "/drive_c/WA2/WA2_full_menu.exe"
typedef enum { RuntimeDeploy_Validating, RuntimeDeploy_Copying, RuntimeDeploy_Verifying } RuntimeDeployPhase;
typedef void (*RuntimeDeployProgress)(RuntimeDeployPhase phase, u64 bytes_done,
                                      u64 bytes_total, const char *path, void *context);
void runtime_deploy_set_progress(RuntimeDeployProgress callback, void *context);
Result runtime_deploy(FsStorage *storage, u64 offset, u64 size, bool *changed);
