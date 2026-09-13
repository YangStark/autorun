/*
 * Switch software-window build: D3DKMT / NtGdiDdDDI* GPU kernel thunks stubbed.
 *
 * The real d3dkmt.c pulls in the full d3d9/10/11/12/dxgi header chain (for
 * struct-layout cross-checks) which the Switch build does not generate, and
 * implements GPU resource sharing irrelevant to the software-rendered window
 * path.  These stubs keep every NtGdiDdDDI* syscall entry point defined so the
 * win32u syscall table (KeServiceDescriptorTable[1]) stays complete, and keep
 * the d3dkmt_* helpers opengl.c references resolvable.  Restore the real file
 * when a GPU backend lands.
 *
 * The adapter and device entry points are real, though: wined3d refuses to
 * create an adapter without them, so d3d9 needs them before it can reach the
 * OpenGL backend.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <pthread.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
/* ntgdi_private.h pulls the full header chain (wingdi/winbase -> SYSTEMTIME)
 * in the order ntgdi.h/winspool.h expect; mirror the real d3dkmt.c. */
#include "ntgdi_private.h"
#include "win32u_private.h"
#include "ntuser_private.h"

/* wined3d opens a D3DKMT adapter and one device per output before it creates
 * any d3d9 device, so those four entry points have to work even without a GPU
 * kernel interface: hand out handles and remember what each one is. */

#define D3DKMT_HANDLE_BIT 0x40000000
#define D3DKMT_MAX_OBJECTS 256

static pthread_mutex_t d3dkmt_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char d3dkmt_objects[D3DKMT_MAX_OBJECTS];

static D3DKMT_HANDLE alloc_object_handle( enum d3dkmt_type type )
{
    D3DKMT_HANDLE handle = 0;
    unsigned int index;

    pthread_mutex_lock( &d3dkmt_lock );
    for (index = 1; index < D3DKMT_MAX_OBJECTS; index++)
    {
        if (d3dkmt_objects[index]) continue;
        d3dkmt_objects[index] = type;
        handle = (index << 6) | D3DKMT_HANDLE_BIT;
        break;
    }
    pthread_mutex_unlock( &d3dkmt_lock );

    return handle;
}

static BOOL object_handle_is( D3DKMT_HANDLE handle, enum d3dkmt_type type )
{
    unsigned int index = (handle & ~0xc0000000) >> 6;
    BOOL ret;

    if (index >= D3DKMT_MAX_OBJECTS) return FALSE;
    pthread_mutex_lock( &d3dkmt_lock );
    ret = d3dkmt_objects[index] == type;
    pthread_mutex_unlock( &d3dkmt_lock );

    return ret;
}

static BOOL free_object_handle( D3DKMT_HANDLE handle, enum d3dkmt_type type )
{
    unsigned int index = (handle & ~0xc0000000) >> 6;
    BOOL ret;

    if (index >= D3DKMT_MAX_OBJECTS) return FALSE;
    pthread_mutex_lock( &d3dkmt_lock );
    if ((ret = d3dkmt_objects[index] == type)) d3dkmt_objects[index] = 0;
    pthread_mutex_unlock( &d3dkmt_lock );

    return ret;
}

/* --- NtGdiDdDDI* syscall entry points (auto-generated from ntgdi.h) --- */
NTSTATUS WINAPI NtGdiDdDDIAcquireKeyedMutex( D3DKMT_ACQUIREKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIAcquireKeyedMutex2( D3DKMT_ACQUIREKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICheckOcclusion( const D3DKMT_CHECKOCCLUSION *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICheckVidPnExclusiveOwnership( const D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICloseAdapter( const D3DKMT_CLOSEADAPTER *desc )
{
    if (!desc || !desc->hAdapter) return STATUS_INVALID_PARAMETER;
    if (!free_object_handle( desc->hAdapter, D3DKMT_ADAPTER )) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI NtGdiDdDDICreateAllocation( D3DKMT_CREATEALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateAllocation2( D3DKMT_CREATEALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateDevice( D3DKMT_CREATEDEVICE *desc )
{
    D3DKMT_HANDLE handle;

    if (!desc) return STATUS_INVALID_PARAMETER;
    if (!object_handle_is( desc->hAdapter, D3DKMT_ADAPTER )) return STATUS_INVALID_PARAMETER;
    if (!(handle = alloc_object_handle( D3DKMT_DEVICE ))) return STATUS_NO_MEMORY;

    desc->hDevice = handle;
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI NtGdiDdDDICreateKeyedMutex( D3DKMT_CREATEKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateKeyedMutex2( D3DKMT_CREATEKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateSynchronizationObject( D3DKMT_CREATESYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDICreateSynchronizationObject2( D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyAllocation( const D3DKMT_DESTROYALLOCATION *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyAllocation2( const D3DKMT_DESTROYALLOCATION2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroyDevice( const D3DKMT_DESTROYDEVICE *desc )
{
    if (!desc || !desc->hDevice) return STATUS_INVALID_PARAMETER;
    if (!free_object_handle( desc->hDevice, D3DKMT_DEVICE )) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI NtGdiDdDDIDestroyKeyedMutex( const D3DKMT_DESTROYKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIDestroySynchronizationObject( const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIEscape( const D3DKMT_ESCAPE *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenAdapterFromHdc( D3DKMT_OPENADAPTERFROMHDC *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenAdapterFromLuid( D3DKMT_OPENADAPTERFROMLUID *desc )
{
    D3DKMT_HANDLE handle;

    if (!desc) return STATUS_INVALID_PARAMETER;
    if (!(handle = alloc_object_handle( D3DKMT_ADAPTER ))) return STATUS_NO_MEMORY;

    desc->hAdapter = handle;
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutex( D3DKMT_OPENKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutex2( D3DKMT_OPENKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenKeyedMutexFromNtHandle( D3DKMT_OPENKEYEDMUTEXFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenNtHandleFromName( D3DKMT_OPENNTHANDLEFROMNAME *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResource( D3DKMT_OPENRESOURCE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResource2( D3DKMT_OPENRESOURCE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenResourceFromNtHandle( D3DKMT_OPENRESOURCEFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSynchronizationObject( D3DKMT_OPENSYNCHRONIZATIONOBJECT *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectFromNtHandle( D3DKMT_OPENSYNCOBJECTFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectFromNtHandle2( D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIOpenSyncObjectNtHandleFromName( D3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryAdapterInfo( D3DKMT_QUERYADAPTERINFO *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryResourceInfo( D3DKMT_QUERYRESOURCEINFO *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryResourceInfoFromNtHandle( D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryStatistics( D3DKMT_QUERYSTATISTICS *stats ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIQueryVideoMemoryInfo( D3DKMT_QUERYVIDEOMEMORYINFO *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIReleaseKeyedMutex( D3DKMT_RELEASEKEYEDMUTEX *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIReleaseKeyedMutex2( D3DKMT_RELEASEKEYEDMUTEX2 *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISetQueuedLimit( D3DKMT_SETQUEUEDLIMIT *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISetVidPnSourceOwner( const D3DKMT_SETVIDPNSOURCEOWNER *desc ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIShareObjects( UINT count, const D3DKMT_HANDLE *handles, OBJECT_ATTRIBUTES *attr, UINT access, HANDLE *handle ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDISignalSynchronizationObjectFromCpu( const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *params ) { return STATUS_PROCEDURE_NOT_FOUND; }
NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObjectFromCpu( const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *params ) { return STATUS_PROCEDURE_NOT_FOUND; }

/* --- d3dkmt_* helpers referenced by opengl.c (GPU resource sharing) --- */
int d3dkmt_object_get_fd( D3DKMT_HANDLE local ) { return -1; }
NTSTATUS d3dkmt_destroy_mutex( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }
D3DKMT_HANDLE d3dkmt_create_resource( int fd, D3DKMT_HANDLE *global ) { if (global) *global = 0; return 0; }
D3DKMT_HANDLE d3dkmt_open_resource( D3DKMT_HANDLE global, HANDLE shared, D3DKMT_HANDLE *mutex_local, D3DKMT_HANDLE *sync_local ) { if (mutex_local) *mutex_local = 0; if (sync_local) *sync_local = 0; return 0; }
NTSTATUS d3dkmt_destroy_resource( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }
D3DKMT_HANDLE d3dkmt_create_sync( int fd, D3DKMT_HANDLE *global ) { if (global) *global = 0; return 0; }
D3DKMT_HANDLE d3dkmt_open_sync( D3DKMT_HANDLE global, HANDLE shared ) { return 0; }
NTSTATUS d3dkmt_destroy_sync( D3DKMT_HANDLE local ) { return STATUS_PROCEDURE_NOT_FOUND; }

/* sysparams.c enumerates Vulkan GPUs to build the display device list; no GPU
 * on the software path, so report none (it falls back to a default adapter). */
BOOL get_vulkan_gpus( struct list *gpus ) { (void)gpus; return FALSE; }
