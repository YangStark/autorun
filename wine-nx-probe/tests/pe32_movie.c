/*
 * Plays a movie through DirectShow, as games do: pe32-movie.exe FILE [SECONDS [SEEK]]
 *
 * Prints RenderFile's result, every filter of the graph with its pins and the
 * media types they connected with, the duration, and how the run ended
 * (EC_COMPLETE, or the timeout), then the position the graph reached.
 * With SEEK, one second into playback it jumps to SEEK seconds.
 */
#define COBJMACROS
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

static void print_graph(IFilterGraph2 *graph)
{
    IEnumFilters *filters;
    IBaseFilter *filter;

    if (FAILED(IFilterGraph2_EnumFilters(graph, &filters)))
        return;
    while (IEnumFilters_Next(filters, 1, &filter, NULL) == S_OK)
    {
        FILTER_INFO info;
        IEnumPins *pins;
        IPin *pin;

        IBaseFilter_QueryFilterInfo(filter, &info);
        wprintf(L"filter \"%ls\"\n", info.achName);
        if (info.pGraph)
            IFilterGraph_Release(info.pGraph);
        if (SUCCEEDED(IBaseFilter_EnumPins(filter, &pins)))
        {
            while (IEnumPins_Next(pins, 1, &pin, NULL) == S_OK)
            {
                AM_MEDIA_TYPE mt;
                PIN_INFO pin_info;

                IPin_QueryPinInfo(pin, &pin_info);
                if (pin_info.pFilter)
                    IBaseFilter_Release(pin_info.pFilter);
                if (IPin_ConnectionMediaType(pin, &mt) == S_OK)
                {
                    wprintf(L"  %ls pin \"%ls\": major %08lx subtype %08lx format %08lx size %lu\n",
                            pin_info.dir == PINDIR_INPUT ? L"input" : L"output", pin_info.achName,
                            mt.majortype.Data1, mt.subtype.Data1, mt.formattype.Data1, mt.cbFormat);
                    CoTaskMemFree(mt.pbFormat);
                    if (mt.pUnk)
                        IUnknown_Release(mt.pUnk);
                }
                else
                {
                    wprintf(L"  %ls pin \"%ls\": not connected\n",
                            pin_info.dir == PINDIR_INPUT ? L"input" : L"output", pin_info.achName);
                }
                IPin_Release(pin);
            }
            IEnumPins_Release(pins);
        }
        IBaseFilter_Release(filter);
    }
    IEnumFilters_Release(filters);
}

int wmain(int argc, WCHAR **argv)
{
    DWORD timeout = argc > 2 ? (DWORD)_wtoi(argv[2]) * 1000 : 60000;
    LONGLONG duration = 0, position = 0;
    IMediaControl *control;
    IMediaSeeking *seeking;
    IFilterGraph2 *graph;
    IMediaEvent *event;
    LONG code = 0;
    HRESULT hr;

    if (argc < 2)
    {
        wprintf(L"usage: pe32-movie.exe FILE [SECONDS]\n");
        return 1;
    }
    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IFilterGraph2, (void **)&graph);
    wprintf(L"FilterGraph hr=%08lx\n", hr);
    if (FAILED(hr))
        return 1;

    hr = IFilterGraph2_RenderFile(graph, argv[1], NULL);
    wprintf(L"RenderFile hr=%08lx\n", hr);
    print_graph(graph);
    if (FAILED(hr))
        return 2;

    IFilterGraph2_QueryInterface(graph, &IID_IMediaSeeking, (void **)&seeking);
    IMediaSeeking_GetDuration(seeking, &duration);
    wprintf(L"duration %.3f s\n", duration / 1e7);

    IFilterGraph2_QueryInterface(graph, &IID_IMediaControl, (void **)&control);
    IFilterGraph2_QueryInterface(graph, &IID_IMediaEvent, (void **)&event);
    hr = IMediaControl_Run(control);
    wprintf(L"Run hr=%08lx\n", hr);
    if (argc > 3)
    {
        LONGLONG seek = (LONGLONG)(_wtof(argv[3]) * 1e7);

        Sleep(1000);
        IMediaSeeking_GetCurrentPosition(seeking, &position);
        hr = IMediaSeeking_SetPositions(seeking, &seek, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
        wprintf(L"at %.3f s, SetPositions(%.3f s) hr=%08lx\n", position / 1e7, seek / 1e7, hr);
    }
    hr = IMediaEvent_WaitForCompletion(event, timeout, &code);
    IMediaSeeking_GetCurrentPosition(seeking, &position);
    wprintf(L"WaitForCompletion hr=%08lx code=%ld position %.3f s\n", hr, code, position / 1e7);
    IMediaControl_Stop(control);

    IMediaEvent_Release(event);
    IMediaControl_Release(control);
    IMediaSeeking_Release(seeking);
    IFilterGraph2_Release(graph);
    CoUninitialize();
    return hr == S_OK && code == EC_COMPLETE ? 0 : 3;
}
