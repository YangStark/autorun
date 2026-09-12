/* Wine-NX waveOut playback checkpoint. Exit 42 validates API completion;
 * hearing the two tones on hardware remains a separate check. */
#include <windows.h>
#include <mmsystem.h>
#include <winternl.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess( HANDLE process, NTSTATUS status );

static short samples[48000 * 2];

static void report( const char *label, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[AUDIO TEST] ";
    WCHAR text[160];
    UNICODE_STRING str;
    unsigned int n = 0, i;
    while (*prefix) text[n++] = *prefix++;
    while (*label && n < 140) text[n++] = *label++;
    text[n++] = ' '; text[n++] = '0'; text[n++] = 'x';
    for (i = 0; i < 8; i++) text[n++] = hex[(value >> (28 - i * 4)) & 15];
    text[n++] = '\n';
    str.Buffer = text;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

void __stdcall start(void)
{
    WAVEFORMATEX format = { WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0 };
    WAVEHDR header = {0};
    HWAVEOUT out = NULL;
    HANDLE event;
    MMRESULT result;
    DWORD failure = 0, begin, wait;
    unsigned int i;
    BOOL prepared = FALSE;

    report( "BEGIN PCM stereo 48000 Hz", 0 );
    report( "waveOutGetNumDevs", waveOutGetNumDevs() );
    event = CreateEventW( NULL, FALSE, FALSE, NULL );
    if (!event) { failure = 1; goto done; }
    report( "before waveOutOpen", 0 );
    result = waveOutOpen( &out, WAVE_MAPPER, &format, (DWORD_PTR)event, 0, CALLBACK_EVENT );
    report( "waveOutOpen", result );
    if (result) { failure = 2; goto cleanup; }

    /* Half a second left, then half a second right; a quiet triangle wave.
     * Integer synthesis keeps this probe independent of CRT and libm. */
    for (i = 0; i < 48000; i++)
    {
        unsigned int period = i < 24000 ? 120 : 80;
        int phase = i % period;
        int value = phase < (int)period / 2 ? phase : (int)period - phase;
        value = (value * 8192 / (int)period) - 2048;
        samples[i * 2] = i < 24000 ? value : 0;
        samples[i * 2 + 1] = i >= 24000 ? value : 0;
    }
    header.lpData = (char *)samples;
    header.dwBufferLength = sizeof(samples);
    result = waveOutPrepareHeader( out, &header, sizeof(header) );
    report( "waveOutPrepareHeader", result );
    if (result) { failure = 3; goto cleanup; }
    prepared = TRUE;
    ResetEvent( event ); /* discard the device-open notification */
    begin = GetTickCount();
    result = waveOutWrite( out, &header, sizeof(header) );
    report( "waveOutWrite", result );
    if (result) { failure = 4; goto cleanup; }
    do
    {
        wait = WaitForSingleObject( event, 250 );
        if (wait == WAIT_FAILED) { failure = 5; break; }
        if (GetTickCount() - begin >= 10000) { failure = 6; break; }
    } while (!(header.dwFlags & WHDR_DONE));
    report( "completion milliseconds", GetTickCount() - begin );
    report( "header flags", header.dwFlags );
    if (!failure && GetTickCount() - begin < 750) failure = 7;

cleanup:
    if (out)
    {
        result = waveOutReset( out );
        report( "waveOutReset", result );
        if (result && !failure) failure = 8;
        if (prepared)
        {
            result = waveOutUnprepareHeader( out, &header, sizeof(header) );
            report( "waveOutUnprepareHeader", result );
            if (result && !failure) failure = 9;
        }
        result = waveOutClose( out );
        report( "waveOutClose", result );
        if (result && !failure) failure = 10;
    }
    CloseHandle( event );
done:
    report( failure ? "FAIL" : "PASS API; verify left then right tone", failure );
    NtTerminateProcess( (HANDLE)-1, failure ? 0x300 | failure : 42 );
    for (;;) {}
}
