#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int Result;
#define MAKERESULT(module, description) ((module) * 1000 + (description))
#define Module_HomebrewLoader 1
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)

typedef enum { FsDirEntryType_File, FsDirEntryType_Dir } FsDirEntryType;
typedef struct { const u8 *bytes; size_t length; } FsStorage;
typedef struct { int unused; } FsFileSystem;
typedef struct { int fd; bool game_asset; } FsFile;
typedef struct { void *evp; } Sha256Context;
enum { FsOpenMode_Read = 1, FsOpenMode_Write = 2,
       FsReadOption_None = 0, FsWriteOption_None = 0, FsWriteOption_Flush = 1 };

Result fsStorageRead(FsStorage *s, u64 offset, void *out, size_t size);
Result fsOpenSdCardFileSystem(FsFileSystem *fs);
void fsFsClose(FsFileSystem *fs);
Result fsFsGetEntryType(FsFileSystem *fs, const char *path, FsDirEntryType *type);
Result fsFsCreateDirectory(FsFileSystem *fs, const char *path);
Result fsFsCreateFile(FsFileSystem *fs, const char *path, s64 size, u32 flags);
Result fsFsDeleteFile(FsFileSystem *fs, const char *path);
Result fsFsOpenFile(FsFileSystem *fs, const char *path, u32 mode, FsFile *file);
Result fsFsRenameFile(FsFileSystem *fs, const char *oldpath, const char *newpath);
Result fsFsCommit(FsFileSystem *fs);
Result fsFsGetFreeSpace(FsFileSystem *fs, const char *path, s64 *free_bytes);
Result fsFileGetSize(FsFile *f, s64 *size);
Result fsFileRead(FsFile *f, u64 offset, void *out, size_t size, u32 options, u64 *read);
Result fsFileWrite(FsFile *f, u64 offset, const void *data, size_t size, u32 options);
Result fsFileFlush(FsFile *f);
void fsFileClose(FsFile *f);
void sha256ContextCreate(Sha256Context *ctx);
void sha256ContextUpdate(Sha256Context *ctx, const void *data, size_t size);
void sha256ContextGetHash(Sha256Context *ctx, u8 hash[32]);
void sha256CalculateHash(u8 hash[32], const void *data, size_t size);
