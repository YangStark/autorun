/* Exercise the real backend with an audout ownership simulator. */
#include <assert.h>
#include <stdio.h>
#include "../source/audio_unix.c"

static AudioOutBuffer *queued[2];
static unsigned int queued_count, ready;
static BOOL host_started;
Result audoutInitialize(void) { return 0; }
void audoutExit(void) { queued_count = ready = 0; host_started = FALSE; }
Result audoutStartAudioOut(void) { host_started = TRUE; return 0; }
Result audoutStopAudioOut(void) { host_started = FALSE; return 0; }
Result audoutAppendAudioOutBuffer(AudioOutBuffer *b)
{
    unsigned int i;
    assert(queued_count < 2);
    assert(!((UINT_PTR)b->buffer & 0xfff));
    assert(b->data_size <= b->buffer_size);
    for (i = 0; i < queued_count; i++) assert(queued[i] != b);
    queued[queued_count++] = b;
    return 0;
}
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **b, u32 *count)
{
    *count = 0; *b = NULL;
    if (ready && queued_count)
    {
        *b = queued[0]; *count = 1;
        queued[0] = queued[1]; queued_count--; ready--;
    }
    return 0;
}
void armDCacheFlush(void *p, size_t size) { (void)p; (void)size; }
void *memalign(size_t alignment, size_t size)
{ void *p = NULL; if (posix_memalign(&p, alignment, size)) return NULL; return p; }
NTSTATUS WINAPI NtAllocateVirtualMemory(HANDLE proc, void **p, ULONG_PTR bits, SIZE_T *size, ULONG type, ULONG prot)
{
    (void)proc; (void)type; (void)prot;
    assert(bits == 0xffffffff00000000ULL);
    *p = calloc(1, *size); return *p ? 0 : STATUS_NO_MEMORY;
}
NTSTATUS WINAPI NtFreeVirtualMemory(HANDLE proc, void **p, SIZE_T *size, ULONG type)
{ (void)proc; (void)size; (void)type; free(*p); *p = NULL; return 0; }
NTSTATUS WINAPI NtSetEvent(HANDLE event, LONG *state) { (void)event; (void)state; return 0; }
NTSTATUS WINAPI NtWaitForSingleObject(HANDLE h, BOOLEAN alert, const LARGE_INTEGER *time)
{ (void)h; (void)alert; (void)time; return 0; }
NTSTATUS WINAPI NtClose(HANDLE h) { (void)h; return 0; }
NTSTATUS WINAPI NtDelayExecution(BOOLEAN alert, const LARGE_INTEGER *time)
{ (void)alert; (void)time; return 0; }
NTSTATUS WINAPI NtQueryPerformanceCounter(LARGE_INTEGER *n, LARGE_INTEGER *f)
{ n->QuadPart = 10000000; if (f) f->QuadPart = 10000000; return 0; }

int main(void)
{
    struct test_connect_params connect = {0};
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM,2,48000,192000,4,16,0};
    stream_handle handle = 0;
    UINT32 channels = 0;
    struct create_stream_params create = {.flow=eRender, .share=AUDCLNT_SHAREMODE_SHARED,
        .duration=400000, .fmt=&fmt, .channel_count=&channels, .stream=&handle};
    BYTE *data = NULL;
    struct get_render_buffer_params get = {.frames=1440, .data=&data};
    struct release_render_buffer_params put = {.written_frames=1440};
    struct start_params startp;
    struct stop_params stopp;
    struct reset_params resetp;
    struct release_stream_params release;
    struct nx_audio_stream *s;
    unsigned int i;
    nx_test_connect(&connect); assert(connect.priority == Priority_Preferred);
    nx_create_stream(&create); assert(create.result == S_OK && channels == 2 && handle);
    s = nx_stream(handle);
    get.stream = put.stream = handle;
    nx_get_render_buffer(&get); assert(get.result == S_OK && data);
    for (i = 0; i < 1440; i++) { ((short *)data)[i*2] = i; ((short *)data)[i*2+1] = -i; }
    nx_release_render_buffer(&put); assert(put.result == S_OK && s->held == 1440);
    startp.stream = handle; nx_start(&startp);
    assert(startp.result == S_OK && host_started && queued_count == 2);
    assert(s->held == 1440 && s->submitted == 960 && !s->played);
    assert(((short *)queued[0]->buffer)[2] == 1);
    assert(((short *)queued[1]->buffer)[0] == 480);
    nx_pump(s); assert(s->held == 1440 && !s->played);
    ready = 1; nx_pump(s);
    assert(s->held == 960 && s->played == 480 && queued_count == 2);
    assert(((short *)queued[1]->buffer)[0] == 960);
    ready = 2; nx_pump(s);
    assert(!s->held && s->played == 1440 && !queued_count);
    /* Cross the ring boundary, preserving order and silence. */
    get.frames = put.written_frames = 960;
    nx_get_render_buffer(&get); assert(get.result == S_OK);
    put.flags = AUDCLNT_BUFFERFLAGS_SILENT;
    nx_release_render_buffer(&put); nx_pump(s);
    for (i = 0; i < 960; i++) assert(((short *)queued[i/480]->buffer)[(i%480)*2] == 0);
    resetp.stream = handle; nx_reset(&resetp); assert(resetp.result == AUDCLNT_E_NOT_STOPPED);
    stopp.stream = handle; nx_stop(&stopp); assert(stopp.result == S_OK && !host_started);
    nx_reset(&resetp); assert(resetp.result == S_OK && !s->held && !s->played && !queued_count);
    get.frames = s->capacity + 1; nx_get_render_buffer(&get); assert(get.result == AUDCLNT_E_BUFFER_TOO_LARGE);
    get.frames = 1; nx_get_render_buffer(&get); assert(get.result == S_OK);
    nx_get_render_buffer(&get); assert(get.result == AUDCLNT_E_OUT_OF_ORDER);
    put.written_frames = 2; nx_release_render_buffer(&put); assert(put.result == AUDCLNT_E_INVALID_SIZE);
    nx_reset(&resetp); assert(resetp.result == AUDCLNT_E_BUFFER_OPERATION_PENDING);
    put.written_frames = 0; nx_release_render_buffer(&put); assert(put.result == S_OK);
    release.stream = handle; release.timer_thread = NULL; nx_release_stream(&release);
    assert(release.result == S_OK && !active && !queued_count);
    /* Common application format: mono 44.1 kHz float is converted to the
     * Switch's stereo 48 kHz signed-16 stream. */
    {
        WAVEFORMATEX converted = {WAVE_FORMAT_IEEE_FLOAT, 1, 44100, 176400, 4, 32, 0};
        struct create_stream_params converted_create = {.flow=eRender, .share=AUDCLNT_SHAREMODE_SHARED,
            .duration=400000, .fmt=&converted, .channel_count=&channels, .stream=&handle};
        float *source;
        nx_create_stream(&converted_create);
        assert(converted_create.result == S_OK && handle);
        get.stream = put.stream = handle; get.frames = put.written_frames = 441;
        nx_get_render_buffer(&get); assert(get.result == S_OK);
        source = (float *)data;
        for (i = 0; i < 441; i++) source[i] = i == 0 ? 0.5f : 0.0f;
        nx_release_render_buffer(&put); assert(put.result == S_OK && nx_stream(handle)->held == 480);
        release.stream = handle; release.timer_thread = NULL; nx_release_stream(&release);
        assert(release.result == S_OK);
    }
    puts("Audio backend: DMA ownership, ordered playback, ring wrap, silence, reset and buffer errors passed");
    return 0;
}
