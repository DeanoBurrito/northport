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

- `FormatPrinter::GetOutput()` a wrapper around OutputToBuffer(), it allocates and returns a string of the appropriate size.
- `FormatPrinter::OutputToBuffer()` performs the coalescing writes to a pre-allocated buffer, taking into account the buffer length.

## Format specifier extensions
- `%b/%B` treats the input as a boolean value, printing either true/false (or their capitalized counterparts if `%B` is used).
- `%U` Yes a lowercase 'u' means unsigned, but I've taken uppercase to mean `U`nits, specifically binary units. It will print the output in a nice format of KB/MB/GB etc.

## Related source files:
- [Format.h](../../libs/np-syslib/include/Format.h)
- [FormatPrinter.h](../../libs/np-syslib/include/FormatPrinter.h)
- [Format.cpp](../../libs/np-syslib/Format.cpp)
- [FormatPrinter.cpp](../../libs/np-syslib/FormatPrinter.cpp)
