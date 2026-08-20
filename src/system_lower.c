#include "porpoise/system_lower.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char DETAIL_RAW[] = "raw architectural state transfer";
static const char DETAIL_CLOCK[] =
    "host-backed time-base and decrementer behavior is approximate";
static const char DETAIL_MMU[] =
    "state is preserved but MMU and translation side effects are not modeled";
static const char DETAIL_CONTROL[] =
    "state is preserved but hardware control side effects are not modeled";
static const char DETAIL_PERFORMANCE[] =
    "performance-monitor timing and side effects are not modeled";
static const char DETAIL_PVR[] =
    "PVR is embedding-host configured state and defaults to zero";
static const char DETAIL_NOOP[] =
    "the target architecture defines this write as a privileged no-op";
static const char DETAIL_SDA_NOOP[] =
    "the target does not implement this sample register and ignores writes";
static const char DETAIL_RFI[] =
    "interrupt-state restoration is modeled but host interrupt machinery is not";
static const char DETAIL_DCBZ[] =
    "guest memory is zeroed atomically without modeling host cache state";
static const char DETAIL_DCBI[] =
    "the coherent host needs no cache invalidation after the privilege check";
static const char DETAIL_TRAP[] =
    "the trap is delegated to the embedding host adapter";
static const char DETAIL_SYSCALL[] =
    "the system call is delegated to the embedding host adapter";
static const char DETAIL_DCBZ_L[] =
    "locked-cache allocation is unsupported";
static const char DETAIL_UNKNOWN_SPR[] =
    "unknown special-purpose register";
static const char DETAIL_OPAQUE_SPR[] =
    "opaque special-purpose register state is preserved but hardware side effects are not modeled";
static const char DETAIL_MCRXR[] =
    "XER summary, overflow, and carry bits are transferred to the selected CR field and cleared";
static const char DETAIL_READ_ONLY[] =
    "write to a read-only special-purpose register is unsupported";
static const char DETAIL_INVALID[] =
    "operands do not match the annotated instruction word";

typedef struct OperandList {
    char storage[256];
    char *values[4];
    size_t count;
} OperandList;

typedef struct SprDescriptor {
    const char *name;
    uint32_t number;
    PorpoiseSystemStorage storage;
    unsigned int index;
    bool privileged;
    PorpoiseLoweringStatus read_status;
    PorpoiseLoweringStatus write_status;
    const char *read_detail;
    const char *write_detail;
} SprDescriptor;

typedef struct FixedSprAlias {
    const char *mnemonic;
    uint32_t number;
    bool write;
} FixedSprAlias;

#define SPR_ENTRY(name_, number_, storage_, index_, privileged_,              \
                  read_status_, write_status_, read_detail_, write_detail_)   \
    {name_, number_, storage_, index_, privileged_, read_status_,             \
     write_status_, read_detail_, write_detail_}

static const SprDescriptor SPR_DESCRIPTORS[] = {
    SPR_ENTRY("XER", 1U, PORPOISE_SYSTEM_STORAGE_XER, 0U, false,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("LR", 8U, PORPOISE_SYSTEM_STORAGE_LR, 0U, false,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("CTR", 9U, PORPOISE_SYSTEM_STORAGE_CTR, 0U, false,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("DSISR", 18U, PORPOISE_SYSTEM_STORAGE_DSISR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("DAR", 19U, PORPOISE_SYSTEM_STORAGE_DAR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("DEC", 22U, PORPOISE_SYSTEM_STORAGE_DEC, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_CLOCK, DETAIL_CLOCK),
    SPR_ENTRY("SDR1", 25U, PORPOISE_SYSTEM_STORAGE_SDR1, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE, DETAIL_RAW, DETAIL_MMU),
    SPR_ENTRY("SRR0", 26U, PORPOISE_SYSTEM_STORAGE_SRR0, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("SRR1", 27U, PORPOISE_SYSTEM_STORAGE_SRR1, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("SPRG0", 272U, PORPOISE_SYSTEM_STORAGE_SPRG, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("SPRG1", 273U, PORPOISE_SYSTEM_STORAGE_SPRG, 1U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("SPRG2", 274U, PORPOISE_SYSTEM_STORAGE_SPRG, 2U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("SPRG3", 275U, PORPOISE_SYSTEM_STORAGE_SPRG, 3U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("ASR", 280U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 280U, true,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_OPAQUE_SPR, DETAIL_READ_ONLY),
    SPR_ENTRY("EAR", 282U, PORPOISE_SYSTEM_STORAGE_EAR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("PVR", 287U, PORPOISE_SYSTEM_STORAGE_PVR, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_HOST_NOOP, DETAIL_PVR, DETAIL_NOOP),
    SPR_ENTRY("GQR0", 912U, PORPOISE_SYSTEM_STORAGE_GQR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR1", 913U, PORPOISE_SYSTEM_STORAGE_GQR, 1U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR2", 914U, PORPOISE_SYSTEM_STORAGE_GQR, 2U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR3", 915U, PORPOISE_SYSTEM_STORAGE_GQR, 3U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR4", 916U, PORPOISE_SYSTEM_STORAGE_GQR, 4U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR5", 917U, PORPOISE_SYSTEM_STORAGE_GQR, 5U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR6", 918U, PORPOISE_SYSTEM_STORAGE_GQR, 6U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("GQR7", 919U, PORPOISE_SYSTEM_STORAGE_GQR, 7U, true,
              PORPOISE_LOWERED, PORPOISE_LOWERED, DETAIL_RAW, DETAIL_RAW),
    SPR_ENTRY("HID2", 920U, PORPOISE_SYSTEM_STORAGE_HID2, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("WPAR", 921U, PORPOISE_SYSTEM_STORAGE_WPAR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("DMA_U", 922U, PORPOISE_SYSTEM_STORAGE_DMA_UPPER, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("DMA_L", 923U, PORPOISE_SYSTEM_STORAGE_DMA_LOWER, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("UMMCR2", 928U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 928U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_OPAQUE_SPR, DETAIL_READ_ONLY),
    SPR_ENTRY("UBAMR", 935U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 935U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_OPAQUE_SPR, DETAIL_READ_ONLY),
    SPR_ENTRY("UMMCR0", 936U, PORPOISE_SYSTEM_STORAGE_MMCR, 0U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("UPMC1", 937U, PORPOISE_SYSTEM_STORAGE_PMC, 0U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("UPMC2", 938U, PORPOISE_SYSTEM_STORAGE_PMC, 1U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("USIA", 939U, PORPOISE_SYSTEM_STORAGE_SIA, 0U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("UMMCR1", 940U, PORPOISE_SYSTEM_STORAGE_MMCR, 1U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("UPMC3", 941U, PORPOISE_SYSTEM_STORAGE_PMC, 2U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("UPMC4", 942U, PORPOISE_SYSTEM_STORAGE_PMC, 3U, false,
              PORPOISE_APPROXIMATE, PORPOISE_UNSUPPORTED,
              DETAIL_PERFORMANCE, DETAIL_READ_ONLY),
    SPR_ENTRY("USDA", 943U, PORPOISE_SYSTEM_STORAGE_SDA, 0U, false,
              PORPOISE_APPROXIMATE, PORPOISE_HOST_NOOP,
              DETAIL_PERFORMANCE, DETAIL_SDA_NOOP),
    SPR_ENTRY("MMCR2", 944U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 944U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("BAMR", 951U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 951U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("MMCR0", 952U, PORPOISE_SYSTEM_STORAGE_MMCR, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("PMC1", 953U, PORPOISE_SYSTEM_STORAGE_PMC, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("PMC2", 954U, PORPOISE_SYSTEM_STORAGE_PMC, 1U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("SIA", 955U, PORPOISE_SYSTEM_STORAGE_SIA, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("MMCR1", 956U, PORPOISE_SYSTEM_STORAGE_MMCR, 1U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("PMC3", 957U, PORPOISE_SYSTEM_STORAGE_PMC, 2U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("PMC4", 958U, PORPOISE_SYSTEM_STORAGE_PMC, 3U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_PERFORMANCE, DETAIL_PERFORMANCE),
    SPR_ENTRY("SDA", 959U, PORPOISE_SYSTEM_STORAGE_SDA, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_HOST_NOOP,
              DETAIL_PERFORMANCE, DETAIL_SDA_NOOP),
    SPR_ENTRY("DMISS", 976U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 976U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("DCMP", 977U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 977U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("HASH1", 978U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 978U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("HASH2", 979U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 979U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("IMISS", 980U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 980U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("ICMP", 981U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 981U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("RPA", 982U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 982U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("HID0", 1008U, PORPOISE_SYSTEM_STORAGE_HID0, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("HID1", 1009U, PORPOISE_SYSTEM_STORAGE_HID1, 0U, true,
              PORPOISE_LOWERED, PORPOISE_HOST_NOOP, DETAIL_RAW, DETAIL_NOOP),
    SPR_ENTRY("IABR", 1010U, PORPOISE_SYSTEM_STORAGE_IABR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("HID4", 1011U, PORPOISE_SYSTEM_STORAGE_HID4, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("DABR", 1013U, PORPOISE_SYSTEM_STORAGE_DABR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("MSSCR0", 1014U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 1014U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("MSSCR1", 1015U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 1015U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
    SPR_ENTRY("L2CR", 1017U, PORPOISE_SYSTEM_STORAGE_L2CR, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("ICTC", 1019U, PORPOISE_SYSTEM_STORAGE_ICTC, 0U, true,
              PORPOISE_LOWERED, PORPOISE_APPROXIMATE,
              DETAIL_RAW, DETAIL_CONTROL),
    SPR_ENTRY("THRM1", 1020U, PORPOISE_SYSTEM_STORAGE_THERMAL, 0U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_CONTROL, DETAIL_CONTROL),
    SPR_ENTRY("THRM2", 1021U, PORPOISE_SYSTEM_STORAGE_THERMAL, 1U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_CONTROL, DETAIL_CONTROL),
    SPR_ENTRY("THRM3", 1022U, PORPOISE_SYSTEM_STORAGE_THERMAL, 2U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_CONTROL, DETAIL_CONTROL),
    SPR_ENTRY("PIR", 1023U, PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR, 1023U, true,
              PORPOISE_APPROXIMATE, PORPOISE_APPROXIMATE,
              DETAIL_OPAQUE_SPR, DETAIL_OPAQUE_SPR),
};

static const FixedSprAlias FIXED_SPR_ALIASES[] = {
    {"mfxer", 1U, false}, {"mtxer", 1U, true},
    {"mfdsisr", 18U, false}, {"mtdsisr", 18U, true},
    {"mfdar", 19U, false}, {"mtdar", 19U, true},
    {"mfdec", 22U, false}, {"mtdec", 22U, true},
    {"mfsdr1", 25U, false}, {"mtsdr1", 25U, true},
    {"mfsrr0", 26U, false}, {"mtsrr0", 26U, true},
    {"mfsrr1", 27U, false}, {"mtsrr1", 27U, true},
    {"mfear", 282U, false}, {"mtear", 282U, true},
    {"mfpvr", 287U, false},
};

static bool text_equal_ignore_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right)) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool split_operands(const char *text, OperandList *list)
{
    char *cursor;

    if (text == NULL || list == NULL || strlen(text) >= sizeof(list->storage)) {
        return false;
    }
    (void)strcpy(list->storage, text);
    list->count = 0U;
    cursor = list->storage;
    while (*cursor != '\0') {
        char *start;
        char *end;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        if (list->count == sizeof(list->values) / sizeof(list->values[0])) {
            return false;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        if (*cursor == ',') {
            *cursor++ = '\0';
        }
        while (end > start && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        if (*start == '\0') {
            return false;
        }
        list->values[list->count++] = start;
    }
    return true;
}

static bool parse_unsigned(const char *text, uint32_t maximum, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *end != '\0' || parsed > (unsigned long)maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_gpr(const char *text, unsigned int *index)
{
    uint32_t value;

    if (text == NULL || (text[0] != 'r' && text[0] != 'R') ||
        !parse_unsigned(text + 1, 31U, &value)) {
        return false;
    }
    *index = (unsigned int)value;
    return true;
}

static bool parse_immediate16(const char *text, uint32_t *encoded)
{
    char *end;
    long parsed;

    if (text == NULL || *text == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno != 0 || *end != '\0' || parsed < -32768L || parsed > 65535L) {
        return false;
    }
    *encoded = (uint32_t)parsed & UINT32_C(0xFFFF);
    return true;
}

static uint32_t decode_spr_field(uint32_t word)
{
    uint32_t encoded = (word >> 11U) & UINT32_C(0x3FF);

    return ((encoded & UINT32_C(0x1F)) << 5U) |
           ((encoded >> 5U) & UINT32_C(0x1F));
}

static uint32_t encoded_spr_field(uint32_t spr)
{
    return ((spr & UINT32_C(0x1F)) << 5U) |
           ((spr >> 5U) & UINT32_C(0x1F));
}

static bool validate_x_form(uint32_t word, uint32_t primary, uint32_t xo)
{
    return (word >> 26U) == primary &&
           ((word >> 1U) & UINT32_C(0x3FF)) == xo &&
           (word & UINT32_C(1)) == 0U;
}

static bool validate_spr_word(
    uint32_t word,
    bool write,
    unsigned int gpr,
    uint32_t spr_number)
{
    uint32_t xo = write ? UINT32_C(467) : UINT32_C(339);

    return validate_x_form(word, UINT32_C(31), xo) &&
           ((word >> 21U) & UINT32_C(31)) == (uint32_t)gpr &&
           decode_spr_field(word) == spr_number;
}

static bool descriptor_from_number(uint32_t number, SprDescriptor *descriptor)
{
    size_t index;

    for (index = 0U;
         index < sizeof(SPR_DESCRIPTORS) / sizeof(SPR_DESCRIPTORS[0]);
         index++) {
        if (SPR_DESCRIPTORS[index].number == number) {
            *descriptor = SPR_DESCRIPTORS[index];
            return true;
        }
    }

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->number = number;
    descriptor->privileged = true;
    descriptor->read_status = PORPOISE_LOWERED;
    descriptor->write_status = PORPOISE_APPROXIMATE;
    descriptor->read_detail = DETAIL_RAW;
    descriptor->write_detail = DETAIL_MMU;

    if (number >= 528U && number <= 535U) {
        descriptor->storage = (number & 1U) != 0U
                                  ? PORPOISE_SYSTEM_STORAGE_IBAT_LOWER
                                  : PORPOISE_SYSTEM_STORAGE_IBAT_UPPER;
        descriptor->index = (unsigned int)((number - 528U) / 2U);
        return true;
    }
    if (number >= 536U && number <= 543U) {
        descriptor->storage = (number & 1U) != 0U
                                  ? PORPOISE_SYSTEM_STORAGE_DBAT_LOWER
                                  : PORPOISE_SYSTEM_STORAGE_DBAT_UPPER;
        descriptor->index = (unsigned int)((number - 536U) / 2U);
        return true;
    }
    if (number >= 560U && number <= 567U) {
        descriptor->storage = (number & 1U) != 0U
                                  ? PORPOISE_SYSTEM_STORAGE_IBAT_LOWER
                                  : PORPOISE_SYSTEM_STORAGE_IBAT_UPPER;
        descriptor->index = 4U + (unsigned int)((number - 560U) / 2U);
        return true;
    }
    if (number >= 568U && number <= 575U) {
        descriptor->storage = (number & 1U) != 0U
                                  ? PORPOISE_SYSTEM_STORAGE_DBAT_LOWER
                                  : PORPOISE_SYSTEM_STORAGE_DBAT_UPPER;
        descriptor->index = 4U + (unsigned int)((number - 568U) / 2U);
        return true;
    }
    return false;
}

static bool parse_bat_name(const char *text, uint32_t *number)
{
    bool instruction;
    unsigned int index;
    char bank;

    if (text == NULL || strlen(text) != 6U ||
        (toupper((unsigned char)text[0]) != 'I' &&
         toupper((unsigned char)text[0]) != 'D') ||
        toupper((unsigned char)text[1]) != 'B' ||
        toupper((unsigned char)text[2]) != 'A' ||
        toupper((unsigned char)text[3]) != 'T' ||
        text[4] < '0' || text[4] > '7') {
        return false;
    }
    bank = (char)toupper((unsigned char)text[5]);
    if (bank != 'U' && bank != 'L') {
        return false;
    }
    instruction = toupper((unsigned char)text[0]) == 'I';
    index = (unsigned int)(text[4] - '0');
    if (instruction) {
        *number = index < 4U ? 528U + index * 2U : 560U + (index - 4U) * 2U;
    } else {
        *number = index < 4U ? 536U + index * 2U : 568U + (index - 4U) * 2U;
    }
    if (bank == 'L') {
        (*number)++;
    }
    return true;
}

static bool parse_spr(
    const char *text,
    uint32_t *number,
    SprDescriptor *descriptor)
{
    size_t index;

    if (parse_unsigned(text, UINT32_C(1023), number)) {
        if (descriptor_from_number(*number, descriptor)) {
            return true;
        }
        memset(descriptor, 0, sizeof(*descriptor));
        descriptor->number = *number;
        descriptor->storage = PORPOISE_SYSTEM_STORAGE_UNKNOWN;
        descriptor->privileged = true;
        descriptor->read_status = PORPOISE_UNSUPPORTED;
        descriptor->write_status = PORPOISE_UNSUPPORTED;
        descriptor->read_detail = DETAIL_UNKNOWN_SPR;
        descriptor->write_detail = DETAIL_UNKNOWN_SPR;
        return true;
    }
    if (parse_bat_name(text, number)) {
        return descriptor_from_number(*number, descriptor);
    }
    for (index = 0U;
         index < sizeof(SPR_DESCRIPTORS) / sizeof(SPR_DESCRIPTORS[0]);
         index++) {
        if (text_equal_ignore_case(text, SPR_DESCRIPTORS[index].name)) {
            *number = SPR_DESCRIPTORS[index].number;
            *descriptor = SPR_DESCRIPTORS[index];
            return true;
        }
    }
    if (text_equal_ignore_case(text, "DMAU")) {
        return parse_spr("DMA_U", number, descriptor);
    }
    if (text_equal_ignore_case(text, "DMAL")) {
        return parse_spr("DMA_L", number, descriptor);
    }
    return false;
}

static void initialize_instruction(
    PorpoiseSystemInstruction *instruction,
    uint32_t word)
{
    memset(instruction, 0, sizeof(*instruction));
    instruction->operation = PORPOISE_SYSTEM_OPERATION_INVALID;
    instruction->storage = PORPOISE_SYSTEM_STORAGE_NONE;
    instruction->status = PORPOISE_UNSUPPORTED;
    instruction->detail = DETAIL_INVALID;
    instruction->word = word;
}

static PorpoiseSystemResolveResult invalid_instruction(
    PorpoiseSystemInstruction *instruction)
{
    instruction->operation = PORPOISE_SYSTEM_OPERATION_INVALID;
    instruction->status = PORPOISE_UNSUPPORTED;
    instruction->semantic_test = false;
    instruction->detail = DETAIL_INVALID;
    return PORPOISE_SYSTEM_INVALID;
}

/*
 * Keep report metadata narrower than implementation coverage. A transfer is
 * marked tested only when a generated-instruction semantic test exercises
 * that storage and direction, not merely because the shared emitter or
 * runtime helper has a unit test.
 */
static bool spr_transfer_has_semantic_test(
    const SprDescriptor *descriptor,
    bool write)
{
    if (descriptor == NULL) {
        return false;
    }
    if (descriptor->storage == PORPOISE_SYSTEM_STORAGE_XER ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_GQR ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_THERMAL ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR ||
        (write && descriptor->storage == PORPOISE_SYSTEM_STORAGE_SIA) ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_IBAT_UPPER ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_IBAT_LOWER ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_DBAT_UPPER ||
        descriptor->storage == PORPOISE_SYSTEM_STORAGE_DBAT_LOWER) {
        return true;
    }
    return write && descriptor->storage == PORPOISE_SYSTEM_STORAGE_PVR;
}

static void apply_spr_descriptor(
    PorpoiseSystemInstruction *instruction,
    const SprDescriptor *descriptor,
    bool write)
{
    instruction->operation = write
                                 ? PORPOISE_SYSTEM_WRITE_STORAGE
                                 : PORPOISE_SYSTEM_READ_STORAGE;
    instruction->storage = descriptor->storage;
    instruction->storage_index = descriptor->index;
    instruction->spr_number = descriptor->number;
    instruction->requires_supervisor = descriptor->privileged;
    instruction->status = write
                              ? descriptor->write_status
                              : descriptor->read_status;
    instruction->detail = write
                              ? descriptor->write_detail
                              : descriptor->read_detail;
    instruction->semantic_test =
        instruction->status != PORPOISE_UNSUPPORTED &&
        spr_transfer_has_semantic_test(descriptor, write);
}

static PorpoiseSystemResolveResult resolve_spr_transfer(
    const OperandList *operands,
    uint32_t word,
    bool write,
    PorpoiseSystemInstruction *instruction)
{
    SprDescriptor descriptor;
    uint32_t number;
    unsigned int gpr;
    const char *spr_text;
    const char *gpr_text;

    if (operands->count != 2U) {
        return invalid_instruction(instruction);
    }
    spr_text = operands->values[write ? 0U : 1U];
    gpr_text = operands->values[write ? 1U : 0U];
    if (!parse_gpr(gpr_text, &gpr) ||
        !parse_spr(spr_text, &number, &descriptor) ||
        !validate_spr_word(word, write, gpr, number)) {
        return invalid_instruction(instruction);
    }

    apply_spr_descriptor(instruction, &descriptor, write);
    if (write) {
        instruction->source_register = gpr;
    } else {
        instruction->destination_register = gpr;
    }
    return PORPOISE_SYSTEM_RESOLVED;
}

/* Preserve the older index-in-mnemonic spelling as a compatibility alias. */
static bool parse_bat_alias_mnemonic(
    const char *mnemonic,
    bool *write,
    uint32_t *number)
{
    const char *name;

    if (strncmp(mnemonic, "mf", 2U) == 0) {
        *write = false;
    } else if (strncmp(mnemonic, "mt", 2U) == 0) {
        *write = true;
    } else {
        return false;
    }
    name = mnemonic + 2U;
    return parse_bat_name(name, number);
}

static bool parse_indexed_bat_alias_mnemonic(
    const char *mnemonic,
    bool *write,
    bool *instruction,
    bool *lower)
{
    if (mnemonic == NULL || strlen(mnemonic) != 7U ||
        mnemonic[0] != 'm' ||
        (mnemonic[1] != 'f' && mnemonic[1] != 't') ||
        (mnemonic[2] != 'i' && mnemonic[2] != 'd') ||
        mnemonic[3] != 'b' || mnemonic[4] != 'a' || mnemonic[5] != 't' ||
        (mnemonic[6] != 'u' && mnemonic[6] != 'l')) {
        return false;
    }

    *write = mnemonic[1] == 't';
    *instruction = mnemonic[2] == 'i';
    *lower = mnemonic[6] == 'l';
    return true;
}

static uint32_t indexed_bat_spr_number(
    bool instruction,
    bool lower,
    uint32_t index)
{
    uint32_t base = instruction ? 528U : 536U;

    if (lower) {
        base++;
    }
    /* BAT4..BAT7 occupy the second SPR bank, 32 numbers above BAT0. */
    return base + (index & UINT32_C(3)) * 2U +
           ((index & UINT32_C(4)) << 3U);
}

static PorpoiseSystemResolveResult resolve_indexed_bat_alias(
    const char *mnemonic,
    const OperandList *operands,
    uint32_t word,
    PorpoiseSystemInstruction *instruction)
{
    SprDescriptor descriptor;
    uint32_t index;
    uint32_t number;
    unsigned int gpr;
    bool write;
    bool instruction_bat;
    bool lower;
    const char *index_text;
    const char *gpr_text;

    if (!parse_indexed_bat_alias_mnemonic(
            mnemonic, &write, &instruction_bat, &lower)) {
        return PORPOISE_SYSTEM_NOT_RECOGNIZED;
    }
    if (operands->count != 2U) {
        return invalid_instruction(instruction);
    }

    index_text = operands->values[write ? 0U : 1U];
    gpr_text = operands->values[write ? 1U : 0U];
    if (!parse_unsigned(index_text, 7U, &index) ||
        !parse_gpr(gpr_text, &gpr)) {
        return invalid_instruction(instruction);
    }
    number = indexed_bat_spr_number(instruction_bat, lower, index);
    if (!descriptor_from_number(number, &descriptor) ||
        !validate_spr_word(word, write, gpr, number)) {
        return invalid_instruction(instruction);
    }

    apply_spr_descriptor(instruction, &descriptor, write);
    if (write) {
        instruction->source_register = gpr;
    } else {
        instruction->destination_register = gpr;
    }
    return PORPOISE_SYSTEM_RESOLVED;
}

static PorpoiseSystemResolveResult resolve_fixed_spr_alias(
    const char *mnemonic,
    const OperandList *operands,
    uint32_t word,
    PorpoiseSystemInstruction *instruction)
{
    SprDescriptor descriptor;
    uint32_t number = 0U;
    bool write = false;
    bool found = false;
    unsigned int gpr;
    size_t index;

    for (index = 0U;
         index < sizeof(FIXED_SPR_ALIASES) / sizeof(FIXED_SPR_ALIASES[0]);
         index++) {
        if (strcmp(mnemonic, FIXED_SPR_ALIASES[index].mnemonic) == 0) {
            number = FIXED_SPR_ALIASES[index].number;
            write = FIXED_SPR_ALIASES[index].write;
            found = true;
            break;
        }
    }
    if (!found && parse_bat_alias_mnemonic(mnemonic, &write, &number)) {
        found = true;
    }
    if (!found) {
        return PORPOISE_SYSTEM_NOT_RECOGNIZED;
    }
    if (operands->count != 1U ||
        !parse_gpr(operands->values[0], &gpr) ||
        !descriptor_from_number(number, &descriptor) ||
        !validate_spr_word(word, write, gpr, number)) {
        return invalid_instruction(instruction);
    }
    apply_spr_descriptor(instruction, &descriptor, write);
    if (write) {
        instruction->source_register = gpr;
    } else {
        instruction->destination_register = gpr;
    }
    return PORPOISE_SYSTEM_RESOLVED;
}

static uint32_t expand_cr_mask(uint32_t field_mask)
{
    uint32_t result = 0U;
    unsigned int field;

    for (field = 0U; field < 8U; field++) {
        if ((field_mask & (UINT32_C(0x80) >> field)) != 0U) {
            result |= UINT32_C(0xF0000000) >> (field * 4U);
        }
    }
    return result;
}

static bool is_system_mnemonic(const char *mnemonic)
{
    static const char *const names[] = {
        "mfspr", "mtspr", "mfsprg", "mtsprg",
        "mftb", "mftbu", "mttbl", "mttbu",
        "mfmsr", "mtmsr", "mfsr", "mtsr",
        "mtcrf", "mtcr", "mcrxr", "rfi", "dcbz", "dcbi", "dcbz_l",
        "twui", "sc",
    };
    uint32_t ignored_number;
    bool ignored_write;
    bool ignored_instruction;
    bool ignored_lower;
    size_t index;

    for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(mnemonic, names[index]) == 0) {
            return true;
        }
    }
    for (index = 0U;
         index < sizeof(FIXED_SPR_ALIASES) / sizeof(FIXED_SPR_ALIASES[0]);
         index++) {
        if (strcmp(mnemonic, FIXED_SPR_ALIASES[index].mnemonic) == 0) {
            return true;
        }
    }
    return parse_bat_alias_mnemonic(
               mnemonic,
               &ignored_write,
               &ignored_number) ||
           parse_indexed_bat_alias_mnemonic(
               mnemonic,
               &ignored_write,
               &ignored_instruction,
               &ignored_lower);
}

PorpoiseSystemResolveResult porpoise_system_resolve(
    const char *mnemonic,
    const char *operands_text,
    uint32_t word,
    PorpoiseSystemInstruction *instruction)
{
    OperandList operands;
    PorpoiseSystemResolveResult alias_result;
    unsigned int gpr;

    if (mnemonic == NULL || operands_text == NULL || instruction == NULL) {
        return PORPOISE_SYSTEM_INVALID;
    }
    initialize_instruction(instruction, word);
    if (!is_system_mnemonic(mnemonic)) {
        return PORPOISE_SYSTEM_NOT_RECOGNIZED;
    }
    if (!split_operands(operands_text, &operands)) {
        return invalid_instruction(instruction);
    }

    if (strcmp(mnemonic, "mfspr") == 0) {
        return resolve_spr_transfer(&operands, word, false, instruction);
    }
    if (strcmp(mnemonic, "mtspr") == 0) {
        return resolve_spr_transfer(&operands, word, true, instruction);
    }

    alias_result = resolve_indexed_bat_alias(
        mnemonic, &operands, word, instruction);
    if (alias_result != PORPOISE_SYSTEM_NOT_RECOGNIZED) {
        return alias_result;
    }

    alias_result = resolve_fixed_spr_alias(
        mnemonic, &operands, word, instruction);
    if (alias_result != PORPOISE_SYSTEM_NOT_RECOGNIZED) {
        return alias_result;
    }

    if (strcmp(mnemonic, "mfsprg") == 0 ||
        strcmp(mnemonic, "mtsprg") == 0) {
        SprDescriptor descriptor;
        uint32_t index;
        uint32_t number;
        bool write = mnemonic[1] == 't';
        const char *index_text;
        const char *gpr_text;

        if (operands.count != 2U) {
            return invalid_instruction(instruction);
        }
        index_text = operands.values[write ? 0U : 1U];
        gpr_text = operands.values[write ? 1U : 0U];
        if (!parse_unsigned(index_text, 3U, &index) ||
            !parse_gpr(gpr_text, &gpr)) {
            return invalid_instruction(instruction);
        }
        number = 272U + index;
        if (!descriptor_from_number(number, &descriptor) ||
            !validate_spr_word(word, write, gpr, number)) {
            return invalid_instruction(instruction);
        }
        apply_spr_descriptor(instruction, &descriptor, write);
        if (write) {
            instruction->source_register = gpr;
        } else {
            instruction->destination_register = gpr;
        }
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mftb") == 0 ||
        strcmp(mnemonic, "mftbu") == 0) {
        uint32_t number;

        if (operands.count == 0U || operands.count > 2U ||
            !parse_gpr(operands.values[0], &gpr)) {
            return invalid_instruction(instruction);
        }
        if (strcmp(mnemonic, "mftbu") == 0) {
            if (operands.count != 1U) {
                return invalid_instruction(instruction);
            }
            number = 269U;
        } else if (operands.count == 1U) {
            number = 268U;
        } else if (parse_unsigned(operands.values[1], 1023U, &number) &&
                   (number == 268U || number == 269U)) {
            /* The canonical instruction form names TBL/TBU by TBR number. */
        } else {
            return invalid_instruction(instruction);
        }
        if (word != ((UINT32_C(31) << 26U) |
                     ((uint32_t)gpr << 21U) |
                     (encoded_spr_field(number) << 11U) |
                     (UINT32_C(371) << 1U))) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_READ_STORAGE;
        instruction->storage = number == 269U
                                   ? PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER
                                   : PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_CLOCK;
        instruction->spr_number = number;
        instruction->destination_register = gpr;
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mttbl") == 0 ||
        strcmp(mnemonic, "mttbu") == 0) {
        uint32_t number = strcmp(mnemonic, "mttbu") == 0 ? 285U : 284U;

        if (operands.count != 1U || !parse_gpr(operands.values[0], &gpr) ||
            !validate_spr_word(word, true, gpr, number)) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_WRITE_STORAGE;
        instruction->storage = number == 285U
                                   ? PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER
                                   : PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_CLOCK;
        instruction->spr_number = number;
        instruction->source_register = gpr;
        instruction->requires_supervisor = true;
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mfmsr") == 0 || strcmp(mnemonic, "mtmsr") == 0) {
        bool write = mnemonic[1] == 't';
        uint32_t xo = write ? UINT32_C(146) : UINT32_C(83);

        if (operands.count != 1U || !parse_gpr(operands.values[0], &gpr) ||
            word != ((UINT32_C(31) << 26U) |
                     ((uint32_t)gpr << 21U) |
                     (xo << 1U))) {
            return invalid_instruction(instruction);
        }
        instruction->operation = write
                                     ? PORPOISE_SYSTEM_WRITE_STORAGE
                                     : PORPOISE_SYSTEM_READ_STORAGE;
        instruction->storage = PORPOISE_SYSTEM_STORAGE_MSR;
        instruction->status = write ? PORPOISE_APPROXIMATE : PORPOISE_LOWERED;
        instruction->semantic_test = true;
        instruction->detail = write ? DETAIL_CONTROL : DETAIL_RAW;
        instruction->requires_supervisor = true;
        if (write) {
            instruction->source_register = gpr;
        } else {
            instruction->destination_register = gpr;
        }
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mfsr") == 0 || strcmp(mnemonic, "mtsr") == 0) {
        bool write = mnemonic[1] == 't';
        uint32_t segment;
        uint32_t xo = write ? UINT32_C(210) : UINT32_C(595);
        const char *segment_text;
        const char *gpr_text;

        if (operands.count != 2U) {
            return invalid_instruction(instruction);
        }
        segment_text = operands.values[write ? 0U : 1U];
        gpr_text = operands.values[write ? 1U : 0U];
        if (!parse_unsigned(segment_text, 15U, &segment) ||
            !parse_gpr(gpr_text, &gpr) ||
            word != ((UINT32_C(31) << 26U) |
                     ((uint32_t)gpr << 21U) |
                     (segment << 16U) |
                     (xo << 1U))) {
            return invalid_instruction(instruction);
        }
        instruction->operation = write
                                     ? PORPOISE_SYSTEM_WRITE_STORAGE
                                     : PORPOISE_SYSTEM_READ_STORAGE;
        instruction->storage = PORPOISE_SYSTEM_STORAGE_SEGMENT;
        instruction->storage_index = (unsigned int)segment;
        instruction->status = write ? PORPOISE_APPROXIMATE : PORPOISE_LOWERED;
        instruction->semantic_test = true;
        instruction->detail = write ? DETAIL_MMU : DETAIL_RAW;
        instruction->requires_supervisor = true;
        if (write) {
            instruction->source_register = gpr;
        } else {
            instruction->destination_register = gpr;
        }
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mtcrf") == 0 || strcmp(mnemonic, "mtcr") == 0) {
        uint32_t field_mask = UINT32_C(0xFF);

        if (strcmp(mnemonic, "mtcrf") == 0) {
            if (operands.count != 2U ||
                !parse_unsigned(operands.values[0], UINT32_C(0xFF), &field_mask) ||
                !parse_gpr(operands.values[1], &gpr)) {
                return invalid_instruction(instruction);
            }
        } else if (operands.count != 1U ||
                   !parse_gpr(operands.values[0], &gpr)) {
            return invalid_instruction(instruction);
        }
        if (word != ((UINT32_C(31) << 26U) |
                     ((uint32_t)gpr << 21U) |
                     (field_mask << 12U) |
                     (UINT32_C(144) << 1U))) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_MTCRF;
        instruction->status = PORPOISE_LOWERED;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_RAW;
        instruction->source_register = gpr;
        instruction->immediate = field_mask;
        instruction->cr_mask = expand_cr_mask(field_mask);
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "mcrxr") == 0) {
        uint32_t field;

        if (operands.count != 1U ||
            strncmp(operands.values[0], "cr", 2U) != 0 ||
            !parse_unsigned(operands.values[0] + 2U, 7U, &field) ||
            word != ((UINT32_C(31) << 26U) |
                     (field << 23U) |
                     (UINT32_C(512) << 1U))) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_MCRXR;
        instruction->status = PORPOISE_LOWERED;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_MCRXR;
        instruction->storage_index = (unsigned int)field;
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "rfi") == 0) {
        if (operands.count != 0U || word != UINT32_C(0x4C000064)) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_RFI;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_RFI;
        instruction->requires_supervisor = true;
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "dcbz") == 0 || strcmp(mnemonic, "dcbi") == 0 ||
        strcmp(mnemonic, "dcbz_l") == 0) {
        uint32_t xo = strcmp(mnemonic, "dcbi") == 0
                          ? UINT32_C(470)
                          : UINT32_C(1014);

        if (operands.count != 2U ||
            !parse_gpr(operands.values[0], &instruction->base_register) ||
            !parse_gpr(operands.values[1], &instruction->index_register) ||
            (strcmp(mnemonic, "dcbz_l") != 0 &&
             (!validate_x_form(word, UINT32_C(31), xo) ||
              ((word >> 21U) & UINT32_C(31)) != 0U ||
              ((word >> 16U) & UINT32_C(31)) != instruction->base_register ||
              ((word >> 11U) & UINT32_C(31)) != instruction->index_register))) {
            return invalid_instruction(instruction);
        }
        if (strcmp(mnemonic, "dcbz_l") == 0) {
            instruction->operation = PORPOISE_SYSTEM_UNSUPPORTED;
            instruction->status = PORPOISE_UNSUPPORTED;
            instruction->semantic_test = false;
            instruction->detail = DETAIL_DCBZ_L;
        } else if (strcmp(mnemonic, "dcbi") == 0) {
            instruction->operation = PORPOISE_SYSTEM_DCBI;
            instruction->status = PORPOISE_HOST_NOOP;
            instruction->semantic_test = true;
            instruction->detail = DETAIL_DCBI;
            instruction->requires_supervisor = true;
        } else {
            instruction->operation = PORPOISE_SYSTEM_DCBZ;
            instruction->status = PORPOISE_APPROXIMATE;
            instruction->semantic_test = true;
            instruction->detail = DETAIL_DCBZ;
        }
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "twui") == 0) {
        uint32_t textual_immediate;
        uint32_t encoded_immediate = word & UINT32_C(0xFFFF);

        if (operands.count != 2U ||
            !parse_gpr(operands.values[0], &instruction->base_register) ||
            !parse_immediate16(operands.values[1], &textual_immediate) ||
            (word >> 26U) != UINT32_C(3) ||
            ((word >> 21U) & UINT32_C(31)) != UINT32_C(31) ||
            ((word >> 16U) & UINT32_C(31)) != instruction->base_register ||
            (textual_immediate & UINT32_C(0xFFFF)) != encoded_immediate) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_TRAP_IMMEDIATE;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_TRAP;
        instruction->immediate = encoded_immediate < UINT32_C(0x8000)
                                     ? encoded_immediate
                                     : encoded_immediate | UINT32_C(0xFFFF0000);
        return PORPOISE_SYSTEM_RESOLVED;
    }

    if (strcmp(mnemonic, "sc") == 0) {
        if (operands.count != 0U || word != UINT32_C(0x44000002)) {
            return invalid_instruction(instruction);
        }
        instruction->operation = PORPOISE_SYSTEM_CALL;
        instruction->status = PORPOISE_APPROXIMATE;
        instruction->semantic_test = true;
        instruction->detail = DETAIL_SYSCALL;
        return PORPOISE_SYSTEM_RESOLVED;
    }

    return PORPOISE_SYSTEM_NOT_RECOGNIZED;
}

static bool file_printf(FILE *output, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(output, format, arguments);
    va_end(arguments);
    return result >= 0;
}

static bool storage_expression(
    const PorpoiseSystemInstruction *instruction,
    char *buffer,
    size_t capacity)
{
    const char *scalar = NULL;
    int result;

    switch (instruction->storage) {
        case PORPOISE_SYSTEM_STORAGE_MSR: scalar = "state->msr"; break;
        case PORPOISE_SYSTEM_STORAGE_XER: scalar = "state->xer"; break;
        case PORPOISE_SYSTEM_STORAGE_LR: scalar = "state->lr"; break;
        case PORPOISE_SYSTEM_STORAGE_CTR: scalar = "state->ctr"; break;
        case PORPOISE_SYSTEM_STORAGE_SRR0: scalar = "state->srr0"; break;
        case PORPOISE_SYSTEM_STORAGE_SRR1: scalar = "state->srr1"; break;
        case PORPOISE_SYSTEM_STORAGE_DAR: scalar = "state->dar"; break;
        case PORPOISE_SYSTEM_STORAGE_DSISR: scalar = "state->dsisr"; break;
        case PORPOISE_SYSTEM_STORAGE_SDR1: scalar = "state->sdr1"; break;
        case PORPOISE_SYSTEM_STORAGE_EAR: scalar = "state->ear"; break;
        case PORPOISE_SYSTEM_STORAGE_PVR: scalar = "state->pvr"; break;
        case PORPOISE_SYSTEM_STORAGE_HID0: scalar = "state->hid0"; break;
        case PORPOISE_SYSTEM_STORAGE_HID1: scalar = "state->hid1"; break;
        case PORPOISE_SYSTEM_STORAGE_HID2: scalar = "state->hid2"; break;
        case PORPOISE_SYSTEM_STORAGE_HID4: scalar = "state->hid4"; break;
        case PORPOISE_SYSTEM_STORAGE_L2CR: scalar = "state->l2cr"; break;
        case PORPOISE_SYSTEM_STORAGE_ICTC: scalar = "state->ictc"; break;
        case PORPOISE_SYSTEM_STORAGE_WPAR: scalar = "state->wpar"; break;
        case PORPOISE_SYSTEM_STORAGE_DMA_UPPER: scalar = "state->dma_upper"; break;
        case PORPOISE_SYSTEM_STORAGE_DMA_LOWER: scalar = "state->dma_lower"; break;
        case PORPOISE_SYSTEM_STORAGE_IABR: scalar = "state->iabr"; break;
        case PORPOISE_SYSTEM_STORAGE_DABR: scalar = "state->dabr"; break;
        case PORPOISE_SYSTEM_STORAGE_SIA: scalar = "state->sia"; break;
        case PORPOISE_SYSTEM_STORAGE_SDA: scalar = "state->sda"; break;
        default: break;
    }
    if (scalar != NULL) {
        result = snprintf(buffer, capacity, "%s", scalar);
    } else {
        const char *array = NULL;

        switch (instruction->storage) {
            case PORPOISE_SYSTEM_STORAGE_GQR: array = "state->gqr"; break;
            case PORPOISE_SYSTEM_STORAGE_SPRG: array = "state->sprg"; break;
            case PORPOISE_SYSTEM_STORAGE_SEGMENT: array = "state->segment_register"; break;
            case PORPOISE_SYSTEM_STORAGE_IBAT_UPPER: array = "state->ibat_upper"; break;
            case PORPOISE_SYSTEM_STORAGE_IBAT_LOWER: array = "state->ibat_lower"; break;
            case PORPOISE_SYSTEM_STORAGE_DBAT_UPPER: array = "state->dbat_upper"; break;
            case PORPOISE_SYSTEM_STORAGE_DBAT_LOWER: array = "state->dbat_lower"; break;
            case PORPOISE_SYSTEM_STORAGE_MMCR: array = "state->mmcr"; break;
            case PORPOISE_SYSTEM_STORAGE_PMC: array = "state->pmc"; break;
            case PORPOISE_SYSTEM_STORAGE_THERMAL: array = "state->thermal_management"; break;
            case PORPOISE_SYSTEM_STORAGE_OPAQUE_SPR: array = "state->opaque_spr"; break;
            default: return false;
        }
        result = snprintf(
            buffer, capacity, "%s[%u]", array, instruction->storage_index);
    }
    return result >= 0 && (size_t)result < capacity;
}

static bool emit_supervisor_check(FILE *output, uint32_t address)
{
    return file_printf(
        output,
        "    if (!porpoise_require_supervisor(state, UINT32_C(0x%08lX))) return;\n",
        (unsigned long)address);
}

static bool emit_storage_transfer(
    FILE *output,
    const PorpoiseSystemInstruction *instruction,
    uint32_t address)
{
    char expression[96];
    bool write = instruction->operation == PORPOISE_SYSTEM_WRITE_STORAGE;

    if (instruction->status == PORPOISE_UNSUPPORTED) {
        return file_printf(
            output,
            "    (void)porpoise_illegal_instruction(state, UINT32_C(0x%08lX), \"unsupported special-purpose register access\");\n    return;\n",
            (unsigned long)address);
    }
    if (instruction->requires_supervisor &&
        !(instruction->storage == PORPOISE_SYSTEM_STORAGE_MSR && write) &&
        instruction->storage != PORPOISE_SYSTEM_STORAGE_DEC &&
        !((instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER ||
           instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER) &&
          write) &&
        !emit_supervisor_check(output, address)) {
        return false;
    }
    if (instruction->status == PORPOISE_HOST_NOOP) {
        if (instruction->requires_supervisor &&
            (instruction->storage == PORPOISE_SYSTEM_STORAGE_MSR ||
             instruction->storage == PORPOISE_SYSTEM_STORAGE_DEC ||
             instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER ||
             instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER) &&
            !emit_supervisor_check(output, address)) {
            return false;
        }
        return file_printf(output, "    /* Architectural host-equivalent no-op. */\n");
    }

    if (instruction->storage == PORPOISE_SYSTEM_STORAGE_DEC) {
        if (write) {
            return file_printf(
                output,
                "    if (!porpoise_decrementer_write(state, UINT32_C(0x%08lX), state->gpr[%u])) return;\n",
                (unsigned long)address,
                instruction->source_register);
        }
        return file_printf(
            output,
            "    if (!porpoise_decrementer_read(state, UINT32_C(0x%08lX), &state->gpr[%u])) return;\n",
            (unsigned long)address,
            instruction->destination_register);
    }
    if (instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_LOWER ||
        instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER) {
        if (write) {
            return file_printf(
                output,
                "    if (!porpoise_time_base_write_%s(state, UINT32_C(0x%08lX), state->gpr[%u])) return;\n",
                instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER
                    ? "upper"
                    : "lower",
                (unsigned long)address,
                instruction->source_register);
        }
        return file_printf(
            output,
            "    {\n        uint64_t porpoise_tb;\n        if (!porpoise_time_base_read(state, UINT32_C(0x%08lX), &porpoise_tb)) return;\n        state->gpr[%u] = (uint32_t)(porpoise_tb %s);\n    }\n",
            (unsigned long)address,
            instruction->destination_register,
            instruction->storage == PORPOISE_SYSTEM_STORAGE_TIME_BASE_UPPER
                ? ">> 32U"
                : "");
    }
    if (instruction->storage == PORPOISE_SYSTEM_STORAGE_MSR && write) {
        return file_printf(
            output,
            "    if (!porpoise_write_msr(state, UINT32_C(0x%08lX), state->gpr[%u])) return;\n",
            (unsigned long)address,
            instruction->source_register);
    }
    if (!storage_expression(instruction, expression, sizeof(expression))) {
        return false;
    }
    if (write) {
        return file_printf(
            output,
            "    %s = state->gpr[%u];\n",
            expression,
            instruction->source_register);
    }
    return file_printf(
        output,
        "    state->gpr[%u] = %s;\n",
        instruction->destination_register,
        expression);
}

bool porpoise_system_emit(
    FILE *output,
    const PorpoiseSystemInstruction *instruction,
    uint32_t instruction_address)
{
    if (output == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->operation) {
        case PORPOISE_SYSTEM_READ_STORAGE:
        case PORPOISE_SYSTEM_WRITE_STORAGE:
            return emit_storage_transfer(output, instruction, instruction_address);
        case PORPOISE_SYSTEM_MTCRF:
            return file_printf(
                output,
                "    state->cr = (state->cr & ~UINT32_C(0x%08lX)) | (state->gpr[%u] & UINT32_C(0x%08lX));\n",
                (unsigned long)instruction->cr_mask,
                instruction->source_register,
                (unsigned long)instruction->cr_mask);
        case PORPOISE_SYSTEM_MCRXR:
            return file_printf(
                output,
                "    porpoise_cr_set_field(state, %uU, (uint8_t)((state->xer >> 28U) & UINT32_C(0xF)));\n    state->xer &= UINT32_C(0x0FFFFFFF);\n",
                instruction->storage_index);
        case PORPOISE_SYSTEM_RFI:
            return file_printf(
                output,
                "    if (!porpoise_require_supervisor(state, UINT32_C(0x%08lX))) return;\n    {\n        uint32_t porpoise_rfi_target = state->srr0 & ~UINT32_C(3);\n        uint32_t porpoise_rfi_msr = ((state->msr & ~UINT32_C(0x87C0FF73)) | (state->srr1 & UINT32_C(0x87C0FF73))) & ~UINT32_C(0x00040000);\n        if (!porpoise_dispatch_available(porpoise_rfi_target)) {\n            (void)porpoise_illegal_instruction(state, UINT32_C(0x%08lX), \"rfi target is outside generated code\");\n            return;\n        }\n        if (!porpoise_write_msr(state, UINT32_C(0x%08lX), porpoise_rfi_msr)) return;\n        state->pc = porpoise_rfi_target;\n        if (!porpoise_call_address(state, porpoise_rfi_target)) return;\n        return;\n    }\n",
                (unsigned long)instruction_address,
                (unsigned long)instruction_address,
                (unsigned long)instruction_address);
        case PORPOISE_SYSTEM_DCBZ:
            if (instruction->base_register == 0U) {
                return file_printf(
                    output,
                    "    if (!porpoise_cache_block_zero(state, state->gpr[%u])) return;\n",
                    instruction->index_register);
            }
            return file_printf(
                output,
                "    if (!porpoise_cache_block_zero(state, state->gpr[%u] + state->gpr[%u])) return;\n",
                instruction->base_register,
                instruction->index_register);
        case PORPOISE_SYSTEM_DCBI:
            if (instruction->base_register == 0U) {
                return file_printf(
                    output,
                    "    if (!porpoise_data_cache_block_invalidate(state, UINT32_C(0x%08lX), state->gpr[%u])) return;\n",
                    (unsigned long)instruction_address,
                    instruction->index_register);
            }
            return file_printf(
                output,
                "    if (!porpoise_data_cache_block_invalidate(state, UINT32_C(0x%08lX), state->gpr[%u] + state->gpr[%u])) return;\n",
                (unsigned long)instruction_address,
                instruction->base_register,
                instruction->index_register);
        case PORPOISE_SYSTEM_TRAP_IMMEDIATE:
            return file_printf(
                output,
                "    if (!porpoise_trap_event(state, UINT32_C(0x%08lX), PORPOISE_TRAP_ALWAYS, state->gpr[%u], UINT32_C(0x%08lX))) return;\n",
                (unsigned long)instruction_address,
                instruction->base_register,
                (unsigned long)instruction->immediate);
        case PORPOISE_SYSTEM_CALL:
            return file_printf(
                output,
                "    if (!porpoise_system_call_event(state, UINT32_C(0x%08lX))) return;\n",
                (unsigned long)instruction_address);
        case PORPOISE_SYSTEM_UNSUPPORTED:
        case PORPOISE_SYSTEM_OPERATION_INVALID:
            return file_printf(
                output,
                "    (void)porpoise_illegal_instruction(state, UINT32_C(0x%08lX), \"unsupported system instruction\");\n    return;\n",
                (unsigned long)instruction_address);
        default:
            return false;
    }
}
