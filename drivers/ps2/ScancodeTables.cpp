#include <ScancodeTables.h>

namespace Ps2
{

    sl::Opt<npk_key_id> TryParseSet2Base(sl::Span<uint8_t> buffer)
    {
        if (buffer.Size() != 1)
            return {};

        switch (buffer[0])
        {
        default:
            return {};

        case 0x01: return npk_key_id_f9;
        case 0x03: return npk_key_id_f5;
        case 0x04: return npk_key_id_f3;
        case 0x05: return npk_key_id_f1;
        case 0x06: return npk_key_id_f2;
        case 0x07: return npk_key_id_f12;
        case 0x09: return npk_key_id_f10;
        case 0x0A: return npk_key_id_f8;
        case 0x0B: return npk_key_id_f6;
        case 0x0C: return npk_key_id_f4;
        case 0x0D: return npk_key_id_tab;
        case 0x0E: return npk_key_id_tilde;
        case 0x11: return npk_key_id_lalt;
        case 0x12: return npk_key_id_lshift;
        case 0x14: return npk_key_id_lctrl;
        case 0x15: return npk_key_id_q;
        case 0x16: return npk_key_id_num1;
        case 0x1A: return npk_key_id_z;
        case 0x1B: return npk_key_id_s;
        case 0x1C: return npk_key_id_a;
        case 0x1D: return npk_key_id_w;
        case 0x1E: return npk_key_id_num2;
        case 0x21: return npk_key_id_c;
        case 0x22: return npk_key_id_x;
        case 0x23: return npk_key_id_d;
        case 0x24: return npk_key_id_e;
        case 0x25: return npk_key_id_num4;
        case 0x26: return npk_key_id_num3;
        case 0x29: return npk_key_id_space;
        case 0x2A: return npk_key_id_v;
        case 0x2B: return npk_key_id_f;
        case 0x2C: return npk_key_id_t;
        case 0x2D: return npk_key_id_r;
        case 0x2E: return npk_key_id_num5;
        case 0x31: return npk_key_id_n;
        case 0x32: return npk_key_id_b;
        case 0x33: return npk_key_id_h;
        case 0x34: return npk_key_id_g;
        case 0x35: return npk_key_id_y;
        case 0x36: return npk_key_id_num6;
        case 0x3A: return npk_key_id_m;
        case 0x3B: return npk_key_id_j;
        case 0x3C: return npk_key_id_u;
        case 0x3D: return npk_key_id_num7;
        case 0x3E: return npk_key_id_num8;
        case 0x41: return npk_key_id_comma;
        case 0x42: return npk_key_id_k;
        case 0x43: return npk_key_id_i;
        case 0x44: return npk_key_id_o;
        case 0x45: return npk_key_id_num0;
        case 0x46: return npk_key_id_num9;
        case 0x49: return npk_key_id_fullstop;
        case 0x4A: return npk_key_id_forwardslash;
        case 0x4B: return npk_key_id_l;
        case 0x4C: return npk_key_id_semicolon;
        case 0x4D: return npk_key_id_p;
        case 0x4E: return npk_key_id_minus;
        case 0x52: return npk_key_id_singlequote;
        case 0x54: return npk_key_id_lbracket;
        case 0x55: return npk_key_id_equals;
        case 0x58: return npk_key_id_capslock;
        case 0x59: return npk_key_id_rshift;
        case 0x5A: return npk_key_id_enter;
        case 0x5B: return npk_key_id_rbracket;
        case 0x5D: return npk_key_id_backslash;
        case 0x66: return npk_key_id_backspace;
        case 0x69: return npk_key_id_numpad1;
        case 0x6B: return npk_key_id_numpad4;
        case 0x6C: return npk_key_id_numpad7;
        case 0x70: return npk_key_id_numpad0;
        case 0x71: return npk_key_id_numpad_dot;
        case 0x72: return npk_key_id_numpad2;
        case 0x73: return npk_key_id_numpad5;
        case 0x74: return npk_key_id_numpad6;
        case 0x75: return npk_key_id_numpad8;
        case 0x76: return npk_key_id_numpad_escape;
        case 0x77: return npk_key_id_numpad_lock;
        case 0x78: return npk_key_id_f11;
        case 0x79: return npk_key_id_numpad_plus;
        case 0x7A: return npk_key_id_numpad3;
        case 0x7B: return npk_key_id_numpad_minus;
        case 0x7C: return npk_key_id_numpad_multiply;
        case 0x7D: return npk_key_id_numpad9;
        case 0x7E: return npk_key_id_scrolllock;
        case 0x83: return npk_key_id_f7;
        }
    }

    sl::Opt<npk_key_id> TryParseSet2Extended(sl::Span<uint8_t> buffer)
    {
        if (buffer.Size() != 1)
        {
            //theres always one: handle the printscreen edge case
            if (buffer.Size() == 3 && buffer[0] == 0x12 &&
                buffer[1] == 0xE0 && buffer[2] == 0x7C)
                return npk_key_id_printscreen;
            return {};
        }

        switch (buffer[0])
        {
        default: return {};

        case 0x11: return npk_key_id_ralt;
        case 0x14: return npk_key_id_rctrl;
        case 0x1F: return npk_key_id_lgui;
        case 0x27: return npk_key_id_rgui;
        case 0x4A: return npk_key_id_numpad_divide;
        case 0x5A: return npk_key_id_numpad_enter;
        case 0x69: return npk_key_id_end;
        case 0x6B: return npk_key_id_arrowleft;
        case 0x6C: return npk_key_id_home;
        case 0x70: return npk_key_id_insert;
        case 0x71: return npk_key_id_delete;
        case 0x72: return npk_key_id_arrowdown;
        case 0x74: return npk_key_id_arrowright;
        case 0x75: return npk_key_id_arrowup;
        case 0x7A: return npk_key_id_pagedown;
        case 0x7D: return npk_key_id_pageup;
        }
    }
}
