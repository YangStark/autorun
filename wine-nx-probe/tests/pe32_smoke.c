/* Minimal relocatable PE32: exercise a real ntdll import, then exit with 42.
 * No CRT, graphics, kernel32, x87, or guest Unix library is required. */
__declspec(dllimport) long __stdcall NtQuerySystemTime( long long *time );
__declspec(dllimport) long __stdcall NtTerminateProcess( void *process, long status );
void __stdcall start(void)
{
    long long time = 0;
    long status = NtQuerySystemTime( &time );
    if (!status) status = time ? 42 : 43;
    NtTerminateProcess( (void *)-1, status );
    for (;;) {}
}
