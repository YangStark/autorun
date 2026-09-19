/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * USB drives through libusbhsfs: FAT and exFAT volumes mount as ums0: to ums4:,
 * which dlls/ntdll/unix/file.c maps to D: to H:. */
#include <stdio.h>
#include <switch.h>
#include <usbhsfs.h>

extern void wine_nx_runtime_trace( const char *msg );

#define USB_WAIT_NS 5000000000ULL

static int usb_started;

void wine_nx_usb_start(void)
{
    Result rc = usbHsFsInitialize( 0 );
    char buf[64];

    usb_started = R_SUCCEEDED( rc );
    if (usb_started) return;
    snprintf( buf, sizeof(buf), "[USB] mass storage unavailable: 0x%08x", (unsigned)rc );
    wine_nx_runtime_trace( buf );
}

/* Drives mount on libusbhsfs' own thread. Wait for the attached ones, up to
 * five seconds, so a program on a drive plugged in before starting is found. */
void wine_nx_usb_wait(void)
{
    UsbHsInterfaceFilter filter = { .Flags = UsbHsInterfaceFilterFlags_bInterfaceClass,
                                    .bInterfaceClass = USB_CLASS_MASS_STORAGE };
    UsbHsInterface interfaces[8];
    UsbHsFsDevice devices[8];
    u64 start = armGetSystemTick();
    s32 attached = 0;
    u32 i, count;
    char buf[200];

    if (!usb_started) return;
    if (R_FAILED( usbHsQueryAllInterfaces( &filter, interfaces, sizeof(interfaces), &attached ) )) attached = 0;
    while (attached > 0 && usbHsFsGetPhysicalDeviceCount() < (u32)attached &&
           armTicksToNs( armGetSystemTick() - start ) < USB_WAIT_NS)
        svcSleepThread( 50000000 );
    count = usbHsFsListMountedDevices( devices, 8 );
    snprintf( buf, sizeof(buf), "[USB] %d mass storage interfaces, %u volumes mounted after %llu ms",
              (int)attached, (unsigned)count,
              (unsigned long long)(armTicksToNs( armGetSystemTick() - start ) / 1000000) );
    wine_nx_runtime_trace( buf );
    for (i = 0; i < count; i++)
    {
        snprintf( buf, sizeof(buf), "[USB] %s %s, %llu MB%s: %s %s", devices[i].name,
                  LIBUSBHSFS_FS_TYPE_STR( devices[i].fs_type ), (unsigned long long)(devices[i].capacity >> 20),
                  devices[i].write_protect ? ", read-only" : "", devices[i].manufacturer, devices[i].product_name );
        wine_nx_runtime_trace( buf );
    }
}
