#pragma once
/* This file defines the Northport kernel driver API. It's subject to the same license as the rest
 * of the kernel, which is included below for convinience.
 *
 * ---- LICENSE ----
 * MIT License
 *
 * Copyright (c) Dean T.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    npk_input_event_type_key,
} npk_input_event_type;

typedef enum
{
    npk_key_flags_none = 0,
    npk_key_flags_pressed = 1 << 0,
    npk_key_flags_released = 1 << 1,
} npk_key_flags;

typedef enum
{
    npk_key_id_none = 0,

    /* function keys */
    npk_key_id_f0 = 0x100,
    npk_key_id_f1 = 0x101,
    npk_key_id_f2 = 0x102,
    npk_key_id_f3 = 0x103,
    npk_key_id_f4 = 0x104,
    npk_key_id_f5 = 0x105,
    npk_key_id_f6 = 0x106,
    npk_key_id_f7 = 0x107,
    npk_key_id_f8 = 0x108,
    npk_key_id_f9 = 0x109,
    npk_key_id_f10 = 0x10A,
    npk_key_id_f11 = 0x10B,
    npk_key_id_f12 = 0x10C,
    npk_key_id_f13 = 0x10D,
    npk_key_id_f14 = 0x10E,
    npk_key_id_f15 = 0x10F,
    npk_key_id_f16 = 0x110,
    npk_key_id_f17 = 0x111,
    npk_key_id_f18 = 0x112,
    npk_key_id_f19 = 0x113,
    npk_key_id_f20 = 0x114,

    /* numbers */
    npk_key_id_num0 = 0x200,
    npk_key_id_num1 = 0x201,
    npk_key_id_num2 = 0x202,
    npk_key_id_num3 = 0x203,
    npk_key_id_num4 = 0x204,
    npk_key_id_num5 = 0x205,
    npk_key_id_num6 = 0x206,
    npk_key_id_num7 = 0x207,
    npk_key_id_num8 = 0x208,
    npk_key_id_num9 = 0x209,
    npk_key_id_numpad0 = 0x210,
    npk_key_id_numpad1 = 0x211,
    npk_key_id_numpad2 = 0x212,
    npk_key_id_numpad3 = 0x213,
    npk_key_id_numpad4 = 0x214,
    npk_key_id_numpad5 = 0x215,
    npk_key_id_numpad6 = 0x216,
    npk_key_id_numpad7 = 0x217,
    npk_key_id_numpad8 = 0x218,
    npk_key_id_numpad9 = 0x219,

    /* main cluster control */
    npk_key_id_esc = 0x300,
    npk_key_id_escape = npk_key_id_esc,
    npk_key_id_backspace = 0x301,
    npk_key_id_enter = 0x302,
    npk_key_id_return = npk_key_id_enter,
    npk_key_id_space = 0x303,
    npk_key_id_spacebar = npk_key_id_space,
    npk_key_id_lshift = 0x304,
    npk_key_id_rshift = 0x305,
    npk_key_id_lalt = 0x306,
    npk_key_id_ralt = 0x307,
    npk_key_id_lgui = 0x308,
    npk_key_id_lsuper = npk_key_id_lgui,
    npk_key_id_rgui = 0x309,
    npk_key_id_rsuper = npk_key_id_rgui,
    npk_key_id_lctrl = 0x30A,
    npk_key_id_rctrl = 0x30B,
    npk_key_id_tab = 0x30C,
    npk_key_id_capslock = 0x30D,

    /* main cluster letters */
    npk_key_id_a = 0x400,
    npk_key_id_b = 0x401,
    npk_key_id_c = 0x402,
    npk_key_id_d = 0x403,
    npk_key_id_e = 0x405,
    npk_key_id_f = 0x406,
    npk_key_id_g = 0x407,
    npk_key_id_h = 0x408,
    npk_key_id_i = 0x409,
    npk_key_id_j = 0x40A,
    npk_key_id_k = 0x40B,
    npk_key_id_l = 0x40C,
    npk_key_id_m = 0x40D,
    npk_key_id_n = 0x40E,
    npk_key_id_o = 0x40F,
    npk_key_id_p = 0x410,
    npk_key_id_q = 0x411,
    npk_key_id_r = 0x412,
    npk_key_id_s = 0x413,
    npk_key_id_t = 0x414,
    npk_key_id_u = 0x415,
    npk_key_id_v = 0x416,
    npk_key_id_w = 0x417,
    npk_key_id_x = 0x418,
    npk_key_id_y = 0x419,
    npk_key_id_z = 0x41A,

    /* main cluster punctuation */
    npk_key_id_lbracket = 0x500,
    npk_key_id_rbracket = 0x501,
    npk_key_id_semicolon = 0x502,
    npk_key_id_singlequote = 0x503,
    npk_key_id_apostrophe = npk_key_id_singlequote,
    npk_key_id_backslash = 0x504,
    npk_key_id_forwardslash = 0x505,
    npk_key_id_comma = 0x506,
    npk_key_id_fullstop = 0x507,
    npk_key_id_dot = npk_key_id_fullstop,
    npk_key_id_minus = 0x508,
    npk_key_id_dash = npk_key_id_minus,
    npk_key_id_equals = 0x509,
    npk_key_id_tilde = 0x50A,
    npk_key_id_backtick = npk_key_id_tilde,

    /* control cluster */
    npk_key_id_printscreen = 0x600,
    npk_key_id_scrolllock = 0x601,
    npk_key_id_pause = 0x602,
    npk_key_id_insert = 0x603,
    npk_key_id_delete = 0x604,
    npk_key_id_home = 0x605,
    npk_key_id_end = 0x606,
    npk_key_id_pageup = 0x607,
    npk_key_id_pagedown = 0x608,
    npk_key_id_arrowup = 0x609,
    npk_key_id_arrowdown = 0x60A,
    npk_key_id_arrowleft = 0x60B,
    npk_key_id_arrowright = 0x60C,

    /* numpad extras */
    npk_key_id_numpad_lock = 0x700,
    npk_key_id_numpad_dot = 0x701,
    npk_key_id_numpad_escape = 0x702,
    npk_key_id_numpad_plus = 0x703,
    npk_key_id_numpad_minus = 0x704,
    npk_key_id_numpad_multiply = 0x705,
    npk_key_id_numpad_divide = 0x706,
    npk_key_id_numpad_enter = 0x707,
} npk_key_id;

typedef struct 
{
    npk_input_event_type type;
    union 
    {
        struct
        {
            npk_key_flags flags;
            npk_key_id id;
        } key;
    };
} npk_input_event;

void npk_send_magic_key(npk_key_id key);

#ifdef __cplusplus
}
#endif
