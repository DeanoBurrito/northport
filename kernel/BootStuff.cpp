#include <boot/Stivale2.h>

uint8_t kernelStackReserve[8192];

static stivale2_tag stivale5LevelPaging
{
    .identifier = STIVALE2_HEADER_TAG_5LV_PAGING_ID,
    .next = 0
};

static stivale2_header_tag_framebuffer stivaleTagFramebuffer
{
    .tag =
    {
        .identifier = STIVALE2_HEADER_TAG_FRAMEBUFFER_ID,
        .next = (uint64_t)&stivale5LevelPaging
    },
    .framebuffer_width = 0,
    .framebuffer_height = 0,
    .framebuffer_bpp = 0,
    .unused = 0
};

[[gnu::section(".stivale2hdr"), gnu::used]]
static stivale2_header stivaleHdr
{
    .entry_point = 0,
    .stack = (uint64_t)kernelStackReserve + sizeof(kernelStackReserve),
    .flags = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4),
    .tags = (uint64_t)&stivaleTagFramebuffer
};
