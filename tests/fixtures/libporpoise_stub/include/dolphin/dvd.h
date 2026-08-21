#ifndef TEST_LIBPORPOISE_DVD_H
#define TEST_LIBPORPOISE_DVD_H

#include <dolphin/types.h>

#if defined(LIBPORPOISE_PORT)
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DVDCommandBlock DVDCommandBlock;
typedef struct DVDFileInfo DVDFileInfo;

typedef void (*DVDCallback)(s32 result, DVDFileInfo *file_info);
typedef void (*DVDCBCallback)(s32 result, DVDCommandBlock *command_block);

struct DVDCommandBlock {
    DVDCommandBlock *next;
    DVDCommandBlock *prev;
    u32 command;
    s32 state;
    u32 offset;
    u32 length;
    void *addr;
    u32 currTransferSize;
    u32 transferredSize;
    void *id;
    DVDCBCallback callback;
    void *userData;
};

struct DVDFileInfo {
    DVDCommandBlock cBlock;
    u32 startAddr;
    u32 length;
    DVDCallback callback;
#if defined(LIBPORPOISE_PORT)
    FILE *pcFilePtr;
#endif
};

#define DVD_STATE_FATAL_ERROR (-1)
#define DVD_STATE_END 0
#define DVD_STATE_BUSY 1
#define DVD_STATE_WAITING 2
#define DVD_STATE_COVER_CLOSED 3
#define DVD_STATE_CANCELED 10

#define DVD_RESULT_FATAL_ERROR (-1)
#define DVD_MIN_TRANSFER_SIZE 32

void DVDInit(void);
BOOL DVDSetRootDirectory(const char *path);
void *DVDGetFSTLocation(void);
s32 DVDConvertPathToEntrynum(const char *path);
BOOL DVDOpen(const char *filename, DVDFileInfo *file_info);
BOOL DVDFastOpen(s32 entry_number, DVDFileInfo *file_info);
s32 DVDReadPrio(
    DVDFileInfo *file_info,
    void *address,
    s32 length,
    s32 offset,
    s32 priority);
BOOL DVDClose(DVDFileInfo *file_info);
s32 DVDGetCommandBlockStatus(const DVDCommandBlock *command_block);
s32 DVDCancel(DVDCommandBlock *command_block);

#ifdef __cplusplus
}
#endif

#endif
