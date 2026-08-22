#include "porpoise_libporpoise_presentation_private.h"

#if defined(__has_include)
#if __has_include(<simulator/sim_gx_Xfb.hpp>)
#include <simulator/sim_gx_Xfb.hpp>
#define PORPOISE_HAS_HOST_XFB_EXECUTION_STATS 1
#endif
#endif

extern "C" int porpoise_libporpoise_presentation_snapshot(
    uint64_t *presentation_count_out,
    uint32_t *guest_frame_buffer_out)
{
    if (presentation_count_out == nullptr ||
        guest_frame_buffer_out == nullptr) {
        return 0;
    }

#if defined(PORPOISE_HAS_HOST_XFB_EXECUTION_STATS)
    const SIM::GX::Detail::HostXfbExecutionStats stats =
        SIM::GX::Detail::GetHostXfbExecutionStats();
    *presentation_count_out = static_cast<uint64_t>(stats.presentations);
    *guest_frame_buffer_out =
        static_cast<uint32_t>(stats.lastPresentedGuestAddress);
    return 1;
#else
    *presentation_count_out = UINT64_C(0);
    *guest_frame_buffer_out = UINT32_C(0);
    return 0;
#endif
}
