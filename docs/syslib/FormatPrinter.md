# Format Printer

This implementation follows the c-style printf() family of functions pretty closely, with some custom extensions.
The c functions (printf, sprintf, etc ...) are not implemented here, but they're just thin wrappers around a format printer.
The formatter is implemented within `class FormatPrinter`, and currently runs in a single pass over the input text.

The formatter uses a linked list, copying plain text from the input until a format token is reached. At this point the token is processed,
and printed to the next entry of the linked list (no need to realloc the original buffer).

The formatter uses the [StringCulture](StringCulture.md) for its string processing and conversions, so as long a a StringCulture implementation is available for a language, it should 'just work'.

To create use a FormatPrinter, the single constructor requires a c-string as input and the max output length. A zero for the output length results in no limit, and the formatter will print everything it can find.
The input string is not copied, only the pointer, so the string must continue to exist until after `FormatPrinter::FormatAll()` has been called.

Once created, trigger the parsing and printing with `FormatAll(va_args args)`. `args` is expected to match the number of formatting tokens, and any 'read from input' tokens.

The 2 output functions both perform similar duties: copying each segment into a single buffer, end to end, forming a single string for the output.

- `FormatPrinter::GetOutput()` returns an **owning** (you must free this pointer yourself!) c-string. This can easily be cast to a string using the owning
constructor (passing in a `true` after the buffer).
- `FormatPrinter::OutputToBuffer()` performs the coalescing writes to a pre-allocated buffer, taking into account the buffer length.

## Format specifier extensions
- `%b/%B` treats the input as a boolean value, printing either true/false (or their capitalized counterparts if `%B` is used).

## Related source files:
- [syslib/include/Format.h](../../syslib/include/Format.h)
- [syslib/include/FormatPrinter.h](../../syslib/include/FormatPrinter.h)
- [syslib/Format.cpp](../../syslib/Format.cpp)
- [syslib/FormatPrinter.cpp](../../syslib/FormatPrinter.cpp)
