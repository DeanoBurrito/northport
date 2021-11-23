#include <boot/Stivale2.h>

uint8_t kernelStackReserve[8192];

__attribute__((used))
static stivale2_tag stivale5LevelPaging
{
    .identifier = STIVALE2_HEADER_TAG_5LV_PAGING_ID,
    .next = 0
};

__attribute__((used))
static stivale2_header_tag_smp stivaleTagSmp
{
    .tag =
    {
        .identifier = STIVALE2_HEADER_TAG_SMP_ID,
        .next = (uint64_t)&stivale5LevelPaging
    },
    .flags = 0
};

__attribute__((used))
static stivale2_header_tag_framebuffer stivaleTagFramebuffer
{
    .tag =
    {
        .identifier = STIVALE2_HEADER_TAG_FRAMEBUFFER_ID,
        .next = (uint64_t)&stivaleTagSmp
    },
    .framebuffer_width = 0,
    .framebuffer_height = 0,
    .framebuffer_bpp = 0,
    .unused = 0
};

__attribute__((used))
static stivale2_header_tag_any_video stivaleTagAnyVideo
{
    .tag = 
    { 
        .identifier = STIVALE2_HEADER_TAG_ANY_VIDEO_ID,
        .next = (uint64_t)&stivaleTagFramebuffer
    },
    .preference = 0
};

__attribute__((section(".stivale2hdr"), used))
static stivale2_header stivaleHdr
{
    .entry_point = 0,
    .stack = (uint64_t)kernelStackReserve + sizeof(kernelStackReserve),
    .flags = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4),
    .tags = (uint64_t)&stivaleTagAnyVideo
};
