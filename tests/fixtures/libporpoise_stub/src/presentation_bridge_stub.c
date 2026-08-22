#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>

int porpoise_libporpoise_presentation_snapshot(
    uint64_t *presentation_count_out,
    uint32_t *guest_frame_buffer_out)
{
    if (presentation_count_out == NULL || guest_frame_buffer_out == NULL) {
        return 0;
    }
    *presentation_count_out = PorpoiseStubVIPresentationCount();
    *guest_frame_buffer_out =
        PorpoiseStubVICurrentFrameBufferGuestAddress();
    return 1;
}
