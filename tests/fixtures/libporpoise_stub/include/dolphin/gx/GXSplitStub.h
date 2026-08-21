#ifndef TEST_LIBPORPOISE_DOLPHIN_GX_SPLIT_STUB_H
#define TEST_LIBPORPOISE_DOLPHIN_GX_SPLIT_STUB_H

/*
 * Expose the umbrella fixture's common GX types through split SDK headers.
 * The generated compatibility header owns the draw-done callback typedef in
 * split-header mode, matching libPorpoise's public-header layout.
 */
#define PORPOISE_STUB_SPLIT_GX_HEADER_VIEW 1
#include <dolphin/gx.h>
#undef PORPOISE_STUB_SPLIT_GX_HEADER_VIEW
#include <porpoise/sdk_import_contract.h>

#endif
