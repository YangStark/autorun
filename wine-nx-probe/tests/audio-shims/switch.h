#ifndef NX_AUDIO_TEST_SWITCH_H
#define NX_AUDIO_TEST_SWITCH_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint32_t u32;
typedef uint32_t Result;
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
typedef struct AudioOutBuffer { struct AudioOutBuffer *next; void *buffer; uint64_t buffer_size, data_size, data_offset; } AudioOutBuffer;
Result audoutInitialize(void);
void audoutExit(void);
Result audoutStartAudioOut(void);
Result audoutStopAudioOut(void);
Result audoutAppendAudioOutBuffer(AudioOutBuffer *);
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **, u32 *);
void armDCacheFlush(void *, size_t);
void *memalign(size_t, size_t);
#endif
