#!/usr/bin/env python3
"""Run dsound's mixer resamplers (dlls/dsound/mixer.c) on known signals: the
cubic one it now uses to raise a buffer's rate, against the ideal waveform, and
against the FIR one it still uses to lower it, which it must advance exactly
like. WarCraft III's sounds went through the FIR at 48 kHz, on a quarter of a
Switch core."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
mixer = (root / 'dlls/dsound/mixer.c').read_text()

def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char BYTE;
typedef unsigned int UINT, DWORD;
typedef int BOOL;
typedef long long LONG64;
#define DSBPLAY_LOOPING 1
#include "fir.h"

typedef struct { DWORD nChannels, nSamplesPerSec, nBlockAlign; } WAVEFORMATEX;
typedef struct { WAVEFORMATEX *pwfx; float *cp_buffer; DWORD cp_buffer_len; } DirectSoundDevice;
typedef struct IDirectSoundBufferImpl IDirectSoundBufferImpl;
typedef float (*bitsgetfunc)(const IDirectSoundBufferImpl *, BYTE *, DWORD);
typedef void (*bitsputfunc)(const IDirectSoundBufferImpl *, DWORD, DWORD, float);
struct IDirectSoundBufferImpl
{
    WAVEFORMATEX *pwfx;
    DirectSoundDevice *device;
    struct { BYTE *memory; } *buffer;
    BYTE *committedbuff;
    DWORD buflen, sec_mixpos, playflags, writelead, committed_mixpos, mix_channels, firstep;
    BOOL use_committed;
    LONG64 freqAdjustNum, freqAdjustDen;
    float firgain;
    bitsgetfunc get;
    bitsputfunc put;
};

static int audible = 1;
static BOOL secondarybuffer_is_audible(IDirectSoundBufferImpl *dsb) { return audible; }

/* Float frames in, float frames out, as dsound's float formats are. */
static float *output;
static float get_float(const IDirectSoundBufferImpl *dsb, BYTE *base, DWORD channel)
{
    return ((float *)base)[channel];
}
static void put_float(const IDirectSoundBufferImpl *dsb, DWORD pos, DWORD channel, float value)
{
    output[pos / sizeof(float) + channel] = value;
}
'''

tests = r'''
#define PI 3.14159265358979323846

struct stream
{
    IDirectSoundBufferImpl dsb;
    DirectSoundDevice device;
    WAVEFORMATEX in, out;
    struct { BYTE *memory; } storage;
    float *samples;
};

/* A looping buffer of one second at in_rate holding a sine per channel. */
static void open_stream(struct stream *s, DWORD in_rate, DWORD channels, const double *freq)
{
    DWORD i, c;

    memset(s, 0, sizeof(*s));
    s->samples = malloc(in_rate * channels * sizeof(float));
    for (i = 0; i < in_rate; i++)
        for (c = 0; c < channels; c++)
            s->samples[i * channels + c] = 0.5 * sin(2 * PI * freq[c] * i / in_rate);
    s->storage.memory = (BYTE *)s->samples;
    s->in.nChannels = s->out.nChannels = channels;
    s->in.nSamplesPerSec = in_rate;
    s->out.nSamplesPerSec = 48000;
    s->in.nBlockAlign = channels * sizeof(float);
    s->device.pwfx = &s->out;
    s->dsb.pwfx = &s->in;
    s->dsb.device = &s->device;
    s->dsb.buffer = (void *)&s->storage;
    s->dsb.buflen = in_rate * s->in.nBlockAlign;
    s->dsb.playflags = DSBPLAY_LOOPING;
    s->dsb.mix_channels = channels;
    s->dsb.freqAdjustNum = in_rate;
    s->dsb.freqAdjustDen = 48000;
    /* As DSOUND_RecalcFormat sets them. */
    s->dsb.firstep = s->dsb.freqAdjustNum > s->dsb.freqAdjustDen
        ? fir_step * s->dsb.freqAdjustDen / s->dsb.freqAdjustNum : fir_step;
    s->dsb.firgain = (float)s->dsb.firstep / fir_step;
    s->dsb.get = get_float;
    s->dsb.put = put_float;
}

/* cp_fields' own advance of the read position. */
static void advance(struct stream *s, UINT adv)
{
    s->dsb.sec_mixpos = (s->dsb.sec_mixpos + adv * s->in.nBlockAlign) % s->dsb.buflen;
}

/* 10 ms chunks of a 1 kHz and a 3 kHz sine, against the input one sample late. */
static void check_accuracy(DWORD in_rate, DWORD channels)
{
    static const double freq[] = {1000.0, 3000.0};
    struct stream s;
    LONG64 acc = 0, out_pos = 0;
    double worst = 0;
    int chunk, i;
    DWORD c;

    open_stream(&s, in_rate, channels, freq);
    output = calloc(480 * channels, sizeof(float));
    for (chunk = 0; chunk < 300; chunk++)
    {
        advance(&s, cp_fields_resample_cubic(&s.dsb, 480, &acc));
        for (i = 0; i < 480; i++, out_pos++)
            for (c = 0; c < channels; c++)
            {
                double t = (double)out_pos / 48000 + 1.0 / in_rate;
                double error = fabs(output[i * channels + c] - 0.5 * sin(2 * PI * freq[c] * t));
                if (error > worst) worst = error;
            }
    }
    printf("%u Hz, %u channels: worst error %.5f of 0.5 over 3 s\n", in_rate, channels, worst);
    assert(worst < 0.005);
    free(output);
    free(s.samples);
}

/* Both resamplers take the same input for the same output, chunk after chunk. */
static void check_advance(DWORD in_rate)
{
    static const double freq[] = {440.0};
    struct stream a, b;
    LONG64 acc_a = 7, acc_b = 7;
    UINT counts[] = {480, 1, 1024, 333, 4096, 17};
    int n;

    open_stream(&a, in_rate, 1, freq);
    open_stream(&b, in_rate, 1, freq);
    output = calloc(4096, sizeof(float));
    for (n = 0; n < 600; n++)
    {
        UINT count = counts[n % 6];
        UINT adv_a = cp_fields_resample_cubic(&a.dsb, count, &acc_a);
        UINT adv_b = cp_fields_resample(&b.dsb, count, &acc_b);

        assert(adv_a == adv_b && acc_a == acc_b);
        advance(&a, adv_a);
        advance(&b, adv_b);
    }
    free(a.device.cp_buffer);
    free(b.device.cp_buffer);
    free(output);
    free(a.samples);
    free(b.samples);
}

int main(void)
{
    static const double freq[] = {440.0};
    struct stream s;
    LONG64 acc = 0;

    check_accuracy(22050, 1);
    check_accuracy(44100, 2);
    check_accuracy(32000, 2);
    check_advance(22050);
    check_advance(44100);
    check_advance(11025);

    /* A silent buffer still moves on, and writes nothing. */
    open_stream(&s, 22050, 1, freq);
    output = NULL;
    audible = 0;
    assert(cp_fields_resample_cubic(&s.dsb, 480, &acc) == 220 && acc == 24000);
    audible = 1;

    printf("PASS: dsound's cubic resampler\n");
    return 0;
}
'''

source = (fixture + function(mixer, 'static inline float get_current_sample') + '\n' +
          function(mixer, 'static UINT cp_fields_resample(') + '\n' +
          function(mixer, 'static UINT cp_fields_resample_cubic(') + '\n' + tests)
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / 'dsound_resample.c'
    exe = Path(tmp) / 'dsound_resample'
    c_file.write_text(source)
    subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wno-unused-parameter', '-Werror',
                    '-I', str(root / 'dlls/dsound'), '-o', str(exe), str(c_file), '-lm'], check=True)
    subprocess.run([str(exe)], check=True)
