#include <dolphin/card.h>

s32 CARDProbeEx(s32 channel, s32 *mem_size, s32 *sector_size)
{
    (void)channel;
    (void)mem_size;
    (void)sector_size;
    return CARD_RESULT_NOCARD;
}
