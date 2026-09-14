/*
 * AVI splitter
 *
 * Copyright 2026 Wine-NX contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Reads AVI files without GStreamer, for systems where winegstreamer, which
 * normally provides quartz's AVI splitter, is not available. Each video and
 * audio stream gets an output pin carrying the stream's own compressed data:
 * the chunks listed in the idx1 index (or found in the movi list when there is
 * none), timed from the stream header, and decoded by whatever filters the
 * graph connects downstream. */

#include <stdbool.h>
#include <stddef.h>

#include "quartz_private.h"
#include "mmreg.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

#define AVI_FCC(a, b, c, d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

#define AVI_INDEX_LIST     0x00000001
#define AVI_INDEX_KEYFRAME 0x00000010

/* FOURCC media subtypes: the FOURCC or format tag in Data1. */
static const GUID fourcc_subtype = {0x00000000, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

struct avi_chunk
{
    LONGLONG offset;  /* of the data */
    DWORD size;
    bool keyframe;
    REFERENCE_TIME start, end;
};

struct avi_stream
{
    struct strmbase_source pin;
    SourceSeeking seek;

    AM_MEDIA_TYPE mt;
    unsigned int number;
    bool is_video;
    DWORD scale, rate, start, sample_size, suggested_size;

    struct avi_chunk *chunks;
    size_t chunk_count, chunk_capacity;
    DWORD max_chunk_size;

    /* Held by the streaming thread while it delivers a chunk, and by
     * application threads to reposition or stop it. */
    CRITICAL_SECTION flushing_cs;
    CONDITION_VARIABLE eos_cv;
    HANDLE thread;

    /* Used by the streaming thread, and by application threads only while that
     * thread is not running or is kept out by flushing_cs. */
    size_t next;
    bool streaming, need_segment, discontinuity, eos;
};

struct avi_splitter
{
    struct strmbase_filter filter;
    struct strmbase_sink sink;
    IAsyncReader *reader;

    struct avi_stream **streams;
    size_t stream_count, stream_capacity;
};

static inline struct avi_splitter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct avi_splitter, filter);
}

static inline struct avi_splitter *impl_from_strmbase_sink(struct strmbase_sink *iface)
{
    return CONTAINING_RECORD(iface, struct avi_splitter, sink);
}

static inline struct avi_stream *impl_from_strmbase_pin(struct strmbase_pin *iface)
{
    return CONTAINING_RECORD(iface, struct avi_stream, pin.pin);
}

static inline struct avi_stream *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct avi_stream, seek.IMediaSeeking_iface);
}

static DWORD get_dword(const BYTE *data)
{
    return data[0] | (data[1] << 8) | (data[2] << 16) | ((DWORD)data[3] << 24);
}

static HRESULT read_data(struct avi_splitter *filter, LONGLONG offset, LONG size, BYTE *data)
{
    HRESULT hr = IAsyncReader_SyncRead(filter->reader, offset, size, data);
    return hr == S_FALSE ? VFW_E_INVALID_FILE_FORMAT : hr;  /* short read */
}

/* A number of stream units (frames, blocks or samples) as a time. */
static REFERENCE_TIME units_to_time(const struct avi_stream *stream, ULONGLONG units)
{
    if (!stream->rate)
        return 0;
    return (REFERENCE_TIME)((double)units * stream->scale / stream->rate * 10000000.0 + 0.5);
}

static int hex_value(BYTE c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A chunk ID is the stream's number in two hex digits and a type, as in 00dc. */
static struct avi_stream *stream_from_chunk_id(struct avi_splitter *filter, DWORD id)
{
    int high = hex_value(id & 0xff), low = hex_value((id >> 8) & 0xff);
    size_t i;

    if (high < 0 || low < 0)
        return NULL;
    if ((id >> 16) == ('p' | 'c' << 8))
        return NULL;  /* a palette change */
    for (i = 0; i < filter->stream_count; ++i)
    {
        if (filter->streams[i]->number == high * 16 + low)
            return filter->streams[i];
    }
    return NULL;
}

static bool add_chunk(struct avi_stream *stream, LONGLONG offset, DWORD size, bool keyframe)
{
    struct avi_chunk *chunk;

    if (!array_reserve((void **)&stream->chunks, &stream->chunk_capacity,
            stream->chunk_count + 1, sizeof(*stream->chunks)))
        return false;
    chunk = &stream->chunks[stream->chunk_count++];
    chunk->offset = offset;
    chunk->size = size;
    chunk->keyframe = keyframe || !stream->is_video;
    stream->max_chunk_size = max(stream->max_chunk_size, size);
    return true;
}

/* A chunk is one sample when the stream header gives no sample size (video
 * frames, variable-rate audio); otherwise it holds size / sample_size samples. */
static void time_chunks(struct avi_stream *stream)
{
    ULONGLONG units = 0;
    size_t i;

    for (i = 0; i < stream->chunk_count; ++i)
    {
        struct avi_chunk *chunk = &stream->chunks[i];

        if (stream->sample_size)
        {
            chunk->start = units_to_time(stream, stream->start + units / stream->sample_size);
            units += chunk->size;
            chunk->end = units_to_time(stream, stream->start + units / stream->sample_size);
        }
        else
        {
            chunk->start = units_to_time(stream, stream->start + units);
            chunk->end = units_to_time(stream, stream->start + ++units);
        }
    }
}

static const GUID *rgb_subtype(WORD bit_count)
{
    switch (bit_count)
    {
    case 1: return &MEDIASUBTYPE_RGB1;
    case 4: return &MEDIASUBTYPE_RGB4;
    case 8: return &MEDIASUBTYPE_RGB8;
    case 16: return &MEDIASUBTYPE_RGB555;
    case 24: return &MEDIASUBTYPE_RGB24;
    case 32: return &MEDIASUBTYPE_RGB32;
    }
    return NULL;
}

static HRESULT init_video_type(struct avi_stream *stream, const BYTE *strf, DWORD size)
{
    DWORD format_size = offsetof(VIDEOINFOHEADER, bmiHeader) + size;
    BITMAPINFOHEADER header;
    VIDEOINFOHEADER *format;
    const GUID *subtype;

    if (size < sizeof(header))
        return VFW_E_INVALID_FILE_FORMAT;
    memcpy(&header, strf, sizeof(header));
    if (!(format = CoTaskMemAlloc(format_size)))
        return E_OUTOFMEMORY;
    memset(format, 0, offsetof(VIDEOINFOHEADER, bmiHeader));
    memcpy(&format->bmiHeader, strf, size);
    format->AvgTimePerFrame = units_to_time(stream, 1);

    memset(&stream->mt, 0, sizeof(stream->mt));
    stream->mt.majortype = MEDIATYPE_Video;
    if (header.biCompression == BI_RGB && (subtype = rgb_subtype(header.biBitCount)))
    {
        stream->mt.subtype = *subtype;
        stream->mt.bFixedSizeSamples = TRUE;
        stream->mt.lSampleSize = header.biSizeImage;
    }
    else
    {
        stream->mt.subtype = fourcc_subtype;
        stream->mt.subtype.Data1 = header.biCompression;
        stream->mt.bTemporalCompression = TRUE;
    }
    stream->mt.formattype = FORMAT_VideoInfo;
    stream->mt.cbFormat = format_size;
    stream->mt.pbFormat = (BYTE *)format;
    return S_OK;
}

static HRESULT init_audio_type(struct avi_stream *stream, const BYTE *strf, DWORD size)
{
    DWORD format_size = max(size, sizeof(WAVEFORMATEX));
    WAVEFORMATEX *format;

    if (size < 16)  /* PCMWAVEFORMAT */
        return VFW_E_INVALID_FILE_FORMAT;
    if (!(format = CoTaskMemAlloc(format_size)))
        return E_OUTOFMEMORY;
    memset(format, 0, format_size);
    memcpy(format, strf, size);
    if (size < sizeof(WAVEFORMATEX))
        format->cbSize = 0;
    else
        format->cbSize = min(format->cbSize, size - sizeof(WAVEFORMATEX));

    memset(&stream->mt, 0, sizeof(stream->mt));
    stream->mt.majortype = MEDIATYPE_Audio;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
            && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        stream->mt.subtype = ((WAVEFORMATEXTENSIBLE *)format)->SubFormat;
    }
    else
    {
        stream->mt.subtype = fourcc_subtype;
        stream->mt.subtype.Data1 = format->wFormatTag;
    }
    stream->mt.bFixedSizeSamples = TRUE;
    stream->mt.lSampleSize = max(format->nBlockAlign, 1);
    stream->mt.formattype = FORMAT_WaveFormatEx;
    stream->mt.cbFormat = format_size;
    stream->mt.pbFormat = (BYTE *)format;
    return S_OK;
}

static const struct strmbase_source_ops source_ops;
static const IMediaSeekingVtbl seeking_vtbl;

static HRESULT WINAPI avi_change_current(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI avi_change_stop(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI avi_change_rate(IMediaSeeking *iface)
{
    return S_OK;
}

/* A strl list; number counts every stream in the file, read or not. */
static HRESULT parse_stream_list(struct avi_splitter *filter, const BYTE *data, DWORD size, unsigned int number)
{
    const BYTE *strh = NULL, *strf = NULL;
    DWORD pos = 0, strh_size = 0, strf_size = 0, type;
    struct avi_stream *stream;
    WCHAR name[20];
    HRESULT hr;

    while (size - pos >= 8)
    {
        DWORD id = get_dword(data + pos), len = get_dword(data + pos + 4);

        if (len > size - pos - 8)
            break;
        if (id == AVI_FCC('s','t','r','h'))
        {
            strh = data + pos + 8;
            strh_size = len;
        }
        else if (id == AVI_FCC('s','t','r','f'))
        {
            strf = data + pos + 8;
            strf_size = len;
        }
        pos += 8 + len + (len & 1);
        if (pos > size)
            break;
    }
    if (!strh || strh_size < 48 || !strf)
        return S_OK;
    type = get_dword(strh);
    if (type != AVI_FCC('v','i','d','s') && type != AVI_FCC('a','u','d','s'))
    {
        TRACE("Skipping stream %u of type %s.\n", number, debugstr_fourcc(type));
        return S_OK;
    }

    if (!array_reserve((void **)&filter->streams, &filter->stream_capacity,
            filter->stream_count + 1, sizeof(*filter->streams)))
        return E_OUTOFMEMORY;
    if (!(stream = calloc(1, sizeof(*stream))))
        return E_OUTOFMEMORY;
    stream->number = number;
    stream->is_video = type == AVI_FCC('v','i','d','s');
    stream->scale = get_dword(strh + 20);
    stream->rate = get_dword(strh + 24);
    stream->start = get_dword(strh + 28);
    stream->suggested_size = get_dword(strh + 36);
    stream->sample_size = get_dword(strh + 44);
    hr = stream->is_video ? init_video_type(stream, strf, strf_size) : init_audio_type(stream, strf, strf_size);
    if (FAILED(hr))
    {
        WARN("Skipping stream %u, hr %#lx.\n", number, hr);
        free(stream);
        return hr == E_OUTOFMEMORY ? hr : S_OK;
    }

    swprintf(name, ARRAY_SIZE(name), L"Stream %02u", number);
    strmbase_source_init(&stream->pin, &filter->filter, name, &source_ops);
    strmbase_seeking_init(&stream->seek, &seeking_vtbl, avi_change_stop, avi_change_current, avi_change_rate);
    InitializeCriticalSectionEx(&stream->flushing_cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    stream->flushing_cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": avi_stream.flushing_cs");
    InitializeConditionVariable(&stream->eos_cv);
    filter->streams[filter->stream_count++] = stream;

    TRACE("Stream %u:\n", number);
    strmbase_dump_media_type(&stream->mt);
    return S_OK;
}

/* The hdrl list, without its type. */
static HRESULT parse_header_list(struct avi_splitter *filter, const BYTE *data, DWORD size)
{
    unsigned int number = 0;
    DWORD pos = 0;
    HRESULT hr;

    while (size - pos >= 8)
    {
        DWORD id = get_dword(data + pos), len = get_dword(data + pos + 4);

        if (len > size - pos - 8)
            break;
        if (id == AVI_FCC('L','I','S','T') && len >= 4 && get_dword(data + pos + 8) == AVI_FCC('s','t','r','l'))
        {
            if (FAILED(hr = parse_stream_list(filter, data + pos + 12, len - 4, number++)))
                return hr;
        }
        pos += 8 + len + (len & 1);
        if (pos > size)
            break;
    }
    return S_OK;
}

/* idx1 entries give each chunk's ID, flags, offset and size. Offsets count
 * from the movi list's type in most files, from the file's start in some. */
static HRESULT read_index(struct avi_splitter *filter, const BYTE *index, DWORD size, LONGLONG movi)
{
    LONGLONG base = movi;
    bool checked = false;
    DWORD i;

    for (i = 0; i < size / 16; ++i)
    {
        const BYTE *entry = index + i * 16;
        DWORD id = get_dword(entry), flags = get_dword(entry + 4);
        DWORD offset = get_dword(entry + 8), len = get_dword(entry + 12);
        struct avi_stream *stream;

        if ((flags & AVI_INDEX_LIST) || !(stream = stream_from_chunk_id(filter, id)))
            continue;
        if (!checked)
        {
            BYTE header[4];

            if (read_data(filter, movi + offset, 4, header) != S_OK || get_dword(header) != id)
            {
                if (read_data(filter, offset, 4, header) == S_OK && get_dword(header) == id)
                    base = 0;
            }
            checked = true;
        }
        if (!add_chunk(stream, base + offset + 8, len, !!(flags & AVI_INDEX_KEYFRAME)))
            return E_OUTOFMEMORY;
    }
    return S_OK;
}

/* Without an index, walk the movi list, descending into rec lists. */
static HRESULT scan_movie_list(struct avi_splitter *filter, LONGLONG pos, LONGLONG end)
{
    BYTE header[12];

    while (end - pos >= 8)
    {
        struct avi_stream *stream;
        DWORD id, len;

        if (read_data(filter, pos, 8, header) != S_OK)
            break;
        id = get_dword(header);
        len = get_dword(header + 4);
        if (id == AVI_FCC('L','I','S','T'))
        {
            if (read_data(filter, pos + 8, 4, header + 8) != S_OK)
                break;
            if (get_dword(header + 8) == AVI_FCC('r','e','c',' '))
            {
                pos += 12;
                continue;
            }
        }
        else if ((stream = stream_from_chunk_id(filter, id)))
        {
            if (!add_chunk(stream, pos + 8, len, true))
                return E_OUTOFMEMORY;
        }
        pos += 8 + (LONGLONG)len + (len & 1);
    }
    return S_OK;
}

static void remove_streams(struct avi_splitter *filter)
{
    size_t i;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct avi_stream *stream = filter->streams[i];

        if (stream->pin.pin.peer)
        {
            if (SUCCEEDED(IMemAllocator_Decommit(stream->pin.pAllocator)))
                IPin_Disconnect(stream->pin.pin.peer);
            IPin_Disconnect(&stream->pin.pin.IPin_iface);
        }
        stream->flushing_cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&stream->flushing_cs);
        strmbase_seeking_cleanup(&stream->seek);
        strmbase_source_cleanup(&stream->pin);
        FreeMediaType(&stream->mt);
        free(stream->chunks);
        free(stream);
    }
    free(filter->streams);
    filter->streams = NULL;
    filter->stream_count = filter->stream_capacity = 0;
    BaseFilterImpl_IncrementPinVersion(&filter->filter);
}

static HRESULT avi_sink_connect(struct strmbase_sink *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct avi_splitter *filter = impl_from_strmbase_sink(iface);
    LONGLONG file_size, available, pos, end, movi = 0, movi_end = 0;
    BYTE header[12], *headers = NULL, *index = NULL;
    size_t i, chunks = 0;
    DWORD index_size = 0;
    HRESULT hr;

    if (FAILED(hr = IPin_QueryInterface(peer, &IID_IAsyncReader, (void **)&filter->reader)))
        return hr;
    IAsyncReader_Length(filter->reader, &file_size, &available);

    if (read_data(filter, 0, 12, header) != S_OK
            || get_dword(header) != AVI_FCC('R','I','F','F') || get_dword(header + 8) != AVI_FCC('A','V','I',' '))
    {
        hr = VFW_E_TYPE_NOT_ACCEPTED;
        goto fail;
    }
    end = min(file_size, 8 + (LONGLONG)get_dword(header + 4));

    for (pos = 12; end - pos >= 8; pos += 8 + (LONGLONG)get_dword(header + 4) + (get_dword(header + 4) & 1))
    {
        DWORD id, len;

        if (read_data(filter, pos, 8, header) != S_OK)
            break;
        id = get_dword(header);
        len = get_dword(header + 4);
        if (id == AVI_FCC('L','I','S','T') && len >= 4 && read_data(filter, pos + 8, 4, header + 8) == S_OK)
        {
            DWORD type = get_dword(header + 8);

            if (type == AVI_FCC('h','d','r','l') && !headers)
            {
                if (len > 16 * 1024 * 1024 || !(headers = malloc(len - 4)))
                {
                    hr = E_OUTOFMEMORY;
                    goto fail;
                }
                if (FAILED(hr = read_data(filter, pos + 12, len - 4, headers))
                        || FAILED(hr = parse_header_list(filter, headers, len - 4)))
                    goto fail;
            }
            else if (type == AVI_FCC('m','o','v','i') && !movi)
            {
                movi = pos + 8;
                movi_end = min(end, pos + 8 + (LONGLONG)len);
            }
        }
        else if (id == AVI_FCC('i','d','x','1') && !index && len >= 16 && len <= 256 * 1024 * 1024)
        {
            if ((index = malloc(len)) && read_data(filter, pos + 8, len, index) == S_OK)
            {
                index_size = len;
            }
            else
            {
                free(index);
                index = NULL;
            }
        }
    }

    if (!filter->stream_count || !movi)
    {
        WARN("No readable streams (%Iu) or no movie list.\n", filter->stream_count);
        hr = VFW_E_INVALID_FILE_FORMAT;
        goto fail;
    }
    hr = S_OK;
    if (index)
        hr = read_index(filter, index, index_size, movi);
    for (i = 0; i < filter->stream_count; ++i)
        chunks += filter->streams[i]->chunk_count;
    if (SUCCEEDED(hr) && !chunks)
        hr = scan_movie_list(filter, movi + 4, movi_end);
    if (FAILED(hr))
        goto fail;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct avi_stream *stream = filter->streams[i];

        time_chunks(stream);
        stream->seek.llDuration = stream->seek.llStop =
                stream->chunk_count ? stream->chunks[stream->chunk_count - 1].end : 0;
        stream->seek.llCurrent = 0;
        TRACE("Stream %u: %Iu chunks, %s.\n", stream->number, stream->chunk_count,
                debugstr_time(stream->seek.llDuration));
    }
    BaseFilterImpl_IncrementPinVersion(&filter->filter);
    free(headers);
    free(index);
    return S_OK;

fail:
    free(headers);
    free(index);
    remove_streams(filter);
    IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
    return hr;
}

static void avi_sink_disconnect(struct strmbase_sink *iface)
{
    struct avi_splitter *filter = impl_from_strmbase_sink(iface);

    remove_streams(filter);
    IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
}

/* Without the registry's byte patterns (wine.inf) the file source offers no
 * subtype, so the header is checked on connection instead. */
static HRESULT avi_sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream)
            && (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_Avi) || IsEqualGUID(&mt->subtype, &GUID_NULL)))
        return S_OK;
    return S_FALSE;
}

static const struct strmbase_sink_ops sink_ops =
{
    .base.pin_query_accept = avi_sink_query_accept,
    .sink_connect = avi_sink_connect,
    .sink_disconnect = avi_sink_disconnect,
};

static HRESULT source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct avi_stream *stream = impl_from_strmbase_pin(iface);

    if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &stream->seek.IMediaSeeking_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT source_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    struct avi_stream *stream = impl_from_strmbase_pin(iface);

    if (!IsEqualGUID(&mt->majortype, &stream->mt.majortype) || !IsEqualGUID(&mt->subtype, &stream->mt.subtype))
        return S_FALSE;
    if (IsEqualGUID(&mt->formattype, &GUID_NULL))
        return S_OK;
    if (!IsEqualGUID(&mt->formattype, &stream->mt.formattype) || mt->cbFormat != stream->mt.cbFormat
            || memcmp(mt->pbFormat, stream->mt.pbFormat, mt->cbFormat))
        return S_FALSE;
    return S_OK;
}

static HRESULT source_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct avi_stream *stream = impl_from_strmbase_pin(iface);

    if (index)
        return VFW_S_NO_MORE_ITEMS;
    return CopyMediaType(mt, &stream->mt);
}

static HRESULT WINAPI source_decide_buffer_size(struct strmbase_source *iface,
        IMemAllocator *allocator, ALLOCATOR_PROPERTIES *props)
{
    struct avi_stream *stream = impl_from_strmbase_pin(&iface->pin);
    ALLOCATOR_PROPERTIES ret_props;

    /* Decoders may hold on to a few samples, as mpg123audiodec keeps two. */
    props->cBuffers = max(props->cBuffers, 4);
    props->cbBuffer = max(props->cbBuffer, max(stream->max_chunk_size, stream->suggested_size));
    props->cbBuffer = max(props->cbBuffer, 1);
    props->cbAlign = max(props->cbAlign, 1);
    return IMemAllocator_SetProperties(allocator, props, &ret_props);
}

static const struct strmbase_source_ops source_ops =
{
    .base.pin_query_interface = source_query_interface,
    .base.pin_query_accept = source_query_accept,
    .base.pin_get_media_type = source_get_media_type,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = source_decide_buffer_size,
};

/* Start at the chunk playing at time, or at the keyframe before it. */
static void seek_stream(struct avi_stream *stream, REFERENCE_TIME time)
{
    size_t i = 0;

    while (i < stream->chunk_count && stream->chunks[i].end <= time)
        ++i;
    while (i && i < stream->chunk_count && !stream->chunks[i].keyframe)
        --i;
    stream->next = i;
    stream->need_segment = true;
    stream->discontinuity = true;
    stream->eos = false;
}

static HRESULT WINAPI seeking_QueryInterface(IMediaSeeking *iface, REFIID iid, void **out)
{
    struct avi_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&stream->pin.pin.IPin_iface, iid, out);
}

static ULONG WINAPI seeking_AddRef(IMediaSeeking *iface)
{
    struct avi_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&stream->pin.pin.IPin_iface);
}

static ULONG WINAPI seeking_Release(IMediaSeeking *iface)
{
    struct avi_stream *stream = impl_from_IMediaSeeking(iface);
    return IPin_Release(&stream->pin.pin.IPin_iface);
}

static HRESULT WINAPI seeking_SetPositions(IMediaSeeking *iface,
        LONGLONG *current, DWORD current_flags, LONGLONG *stop, DWORD stop_flags)
{
    struct avi_stream *stream = impl_from_IMediaSeeking(iface);
    struct avi_splitter *filter = impl_from_strmbase_filter(stream->pin.pin.filter);
    bool flush = !(current_flags & AM_SEEKING_NoFlush) && stream->pin.pin.peer;

    TRACE("stream %p, current %s, current_flags %#lx, stop %s, stop_flags %#lx.\n",
            stream, current ? debugstr_time(*current) : "<null>", current_flags,
            stop ? debugstr_time(*stop) : "<null>", stop_flags);

    /* Stopped, avi_init_stream() starts from the new position. The graph sets
     * each output pin's position through its renderer, so every stream seeks
     * on its own. */
    if (filter->filter.state == State_Stopped || !stream->thread)
    {
        SourceSeekingImpl_SetPositions(iface, current, current_flags, stop, stop_flags);
        return S_OK;
    }

    /* A new stop time alone leaves the stream where it is. */
    if ((current_flags & AM_SEEKING_PositioningBitsMask) == AM_SEEKING_NoPositioning)
    {
        EnterCriticalSection(&stream->flushing_cs);
        SourceSeekingImpl_SetPositions(iface, current, current_flags, stop, stop_flags);
        LeaveCriticalSection(&stream->flushing_cs);
        return S_OK;
    }

    /* Flushing makes downstream refuse and release samples, so the streaming
     * thread finishes its chunk and lets go of flushing_cs. */
    if (flush)
        IPin_BeginFlush(stream->pin.pin.peer);
    EnterCriticalSection(&stream->flushing_cs);
    SourceSeekingImpl_SetPositions(iface, current, current_flags, stop, stop_flags);
    if (flush)
        IPin_EndFlush(stream->pin.pin.peer);
    seek_stream(stream, stream->seek.llCurrent);
    LeaveCriticalSection(&stream->flushing_cs);
    WakeConditionVariable(&stream->eos_cv);
    return S_OK;
}

static const IMediaSeekingVtbl seeking_vtbl =
{
    seeking_QueryInterface,
    seeking_AddRef,
    seeking_Release,
    SourceSeekingImpl_GetCapabilities,
    SourceSeekingImpl_CheckCapabilities,
    SourceSeekingImpl_IsFormatSupported,
    SourceSeekingImpl_QueryPreferredFormat,
    SourceSeekingImpl_GetTimeFormat,
    SourceSeekingImpl_IsUsingTimeFormat,
    SourceSeekingImpl_SetTimeFormat,
    SourceSeekingImpl_GetDuration,
    SourceSeekingImpl_GetStopPosition,
    SourceSeekingImpl_GetCurrentPosition,
    SourceSeekingImpl_ConvertTimeFormat,
    seeking_SetPositions,
    SourceSeekingImpl_GetPositions,
    SourceSeekingImpl_GetAvailable,
    SourceSeekingImpl_SetRate,
    SourceSeekingImpl_GetRate,
    SourceSeekingImpl_GetPreroll,
};

static HRESULT send_chunk(struct avi_splitter *filter, struct avi_stream *stream, const struct avi_chunk *chunk)
{
    REFERENCE_TIME start, end;
    IMediaSample *sample;
    BYTE *data;
    HRESULT hr;

    if (stream->need_segment)
    {
        if (FAILED(hr = IPin_NewSegment(stream->pin.pin.peer,
                stream->seek.llCurrent, stream->seek.llStop, stream->seek.dRate)))
            WARN("Failed to deliver new segment, hr %#lx.\n", hr);
        stream->need_segment = false;
    }
    if (!chunk->size)
        return S_OK;  /* a dropped frame */

    if (FAILED(hr = IMemAllocator_GetBuffer(stream->pin.pAllocator, &sample, NULL, NULL, 0)))
    {
        if (hr != VFW_E_NOT_COMMITTED)  /* not stopping */
            WARN("Failed to get a sample, hr %#lx.\n", hr);
        return hr;
    }
    if (IMediaSample_GetSize(sample) < chunk->size)
    {
        ERR("A %lu byte chunk does not fit a %ld byte sample.\n", chunk->size, IMediaSample_GetSize(sample));
        IMediaSample_Release(sample);
        return S_OK;
    }
    IMediaSample_GetPointer(sample, &data);
    if ((hr = read_data(filter, chunk->offset, chunk->size, data)) != S_OK)
    {
        WARN("Failed to read %lu bytes at %s, hr %#lx.\n", chunk->size, wine_dbgstr_longlong(chunk->offset), hr);
        IMediaSample_Release(sample);
        return hr;
    }
    IMediaSample_SetActualDataLength(sample, chunk->size);

    start = chunk->start - stream->seek.llCurrent;
    end = chunk->end - stream->seek.llCurrent;
    if (stream->seek.dRate > 0.0 && stream->seek.dRate != 1.0)
    {
        start = (REFERENCE_TIME)(start / stream->seek.dRate);
        end = (REFERENCE_TIME)(end / stream->seek.dRate);
    }
    IMediaSample_SetTime(sample, &start, &end);
    IMediaSample_SetMediaTime(sample, NULL, NULL);
    IMediaSample_SetSyncPoint(sample, chunk->keyframe);
    IMediaSample_SetPreroll(sample, chunk->end <= stream->seek.llCurrent);
    IMediaSample_SetDiscontinuity(sample, stream->discontinuity);
    stream->discontinuity = false;

    hr = IMemInputPin_Receive(stream->pin.pMemInputPin, sample);
    IMediaSample_Release(sample);
    return hr;
}

static DWORD CALLBACK stream_thread(void *arg)
{
    struct avi_stream *stream = arg;
    struct avi_splitter *filter = impl_from_strmbase_filter(stream->pin.pin.filter);

    TRACE("Starting streaming thread for pin %p.\n", stream);

    for (;;)
    {
        EnterCriticalSection(&stream->flushing_cs);

        if (!stream->streaming)
        {
            LeaveCriticalSection(&stream->flushing_cs);
            break;
        }

        if (stream->eos)
        {
            SleepConditionVariableCS(&stream->eos_cv, &stream->flushing_cs, INFINITE);
            LeaveCriticalSection(&stream->flushing_cs);
            continue;
        }

        if (stream->next < stream->chunk_count
                && (!stream->seek.llStop || stream->chunks[stream->next].start < stream->seek.llStop))
        {
            HRESULT hr = send_chunk(filter, stream, &stream->chunks[stream->next++]);

            if (hr == S_FALSE || hr == VFW_E_WRONG_STATE || hr == VFW_E_NOT_COMMITTED)
            {
                /* Downstream is flushing or stopping: wait for the seek or the stop. */
                stream->eos = true;
            }
            else if (FAILED(hr))
            {
                /* End the stream rather than leave the graph waiting for it. */
                WARN("Stream %u failed, hr %#lx; ending it.\n", stream->number, hr);
                IPin_EndOfStream(stream->pin.pin.peer);
                stream->eos = true;
            }
        }
        else
        {
            TRACE("End of stream.\n");
            IPin_EndOfStream(stream->pin.pin.peer);
            stream->eos = true;
        }

        LeaveCriticalSection(&stream->flushing_cs);
    }

    TRACE("Streaming stopped; exiting.\n");
    return 0;
}

static struct strmbase_pin *avi_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct avi_splitter *filter = impl_from_strmbase_filter(iface);

    if (!index)
        return &filter->sink.pin;
    if (index <= filter->stream_count)
        return &filter->streams[index - 1]->pin.pin;
    return NULL;
}

static void avi_destroy(struct strmbase_filter *iface)
{
    struct avi_splitter *filter = impl_from_strmbase_filter(iface);

    /* Disconnecting the input pin removes the output pins. */
    if (filter->sink.pin.peer)
    {
        IPin_Disconnect(filter->sink.pin.peer);
        IPin_Disconnect(&filter->sink.pin.IPin_iface);
    }
    if (filter->reader)
        IAsyncReader_Release(filter->reader);
    filter->reader = NULL;

    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT avi_init_stream(struct strmbase_filter *iface)
{
    struct avi_splitter *filter = impl_from_strmbase_filter(iface);
    size_t i;

    if (!filter->reader)
        return S_OK;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct avi_stream *stream = filter->streams[i];
        HRESULT hr;

        if (!stream->pin.pin.peer)
            continue;
        if (FAILED(hr = IMemAllocator_Commit(stream->pin.pAllocator)))
            ERR("Failed to commit allocator, hr %#lx.\n", hr);
        /* DirectShow keeps the seek position, and resets to it when it goes
         * from stopped to paused. */
        seek_stream(stream, stream->seek.llCurrent);
        stream->streaming = true;
        stream->thread = CreateThread(NULL, 0, stream_thread, stream, 0, NULL);
    }
    return S_OK;
}

static HRESULT avi_cleanup_stream(struct strmbase_filter *iface)
{
    struct avi_splitter *filter = impl_from_strmbase_filter(iface);
    size_t i;

    if (!filter->reader)
        return S_OK;

    for (i = 0; i < filter->stream_count; ++i)
    {
        struct avi_stream *stream = filter->streams[i];

        if (!stream->thread)
            continue;
        /* Decommitting releases a thread waiting for a sample. */
        IMemAllocator_Decommit(stream->pin.pAllocator);
        EnterCriticalSection(&stream->flushing_cs);
        stream->streaming = false;
        LeaveCriticalSection(&stream->flushing_cs);
        WakeConditionVariable(&stream->eos_cv);
        WaitForSingleObject(stream->thread, INFINITE);
        CloseHandle(stream->thread);
        stream->thread = NULL;
    }
    return S_OK;
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = avi_get_pin,
    .filter_destroy = avi_destroy,
    .filter_init_stream = avi_init_stream,
    .filter_cleanup_stream = avi_cleanup_stream,
};

HRESULT native_avi_splitter_create(IUnknown *outer, IUnknown **out)
{
    struct avi_splitter *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_AviSplitter, &filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"input pin", &sink_ops, NULL);

    TRACE("Created AVI splitter %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
