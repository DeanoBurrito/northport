# System Call Details
This is a dump of details, its more of a reference for the other readme.

## 0x0 - System Call Loopback Test
Does nothing and returns immediately, used for testing system calling mechanism in various forms.

### Args:
- None.

### Returns:
- None.

### Notes:
- Always succeeds. Used to test I havent broken syscalls when I update related code.

## 0x1 - Get Primary Framebuffer
Returns details about the primary framebuffer. Likely to be retired in the near future, but currently allows for graphics from anywhere. Not at all secure, but very fast!

### Args:
- none.

### Returns:
- `arg0`: base address
- `arg1`: bits 31:0 = width in pixels, bits 63:32 = height in pixels
- `arg2`: bits 31:0 = bits per pixel, bits 63:32 = bpp (bytes per row/stride)
- `arg3`: first 4 bytes are left shifts for red/green/blue/reserved subpixels. Upper 4 bytes are masks for r/g/b/R subpixels.

### Notes:
- None.
