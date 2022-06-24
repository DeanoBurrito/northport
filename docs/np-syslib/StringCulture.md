# String Culture

The idea of a `StringCulture` instance is to represent a unique combination of lexical rules, where the definitions of what is a number or letter may change. The base character sets may change or stay the same. This is implemented as a layer under the usual `is_digit` or `is_alpha` functions, meaning we can swap out the definitins these use, but still use existing code. This should allow careful crafted programs to support potentially any language supported by the OS, to a certain degree.

The default (and currently only) culture is Australian english (basically british english), as has definitions as such. Of course most english dialects can use the same culture, because the definitions of the underlying characters do not change. 

## Implementing a String Culture
It's very straight forward! Simply create a class that inherits from `class StringCulture`, and overload all the virtual methods.

This looks a little intimdating as there is a lot, but if your custom culture uses the arabic numbering system, you can reuse the existing number functions (they're templated in `cultures/Helpers.h`).

Check how the default culture uses these for an example.

## Related source files:
- [StringCulture.h](../../libs/np-syslib/include/StringCulture.h)
- [StringCulture.cpp](../../libs/np-syslib/StringCulture.cpp)
- [Helpers.h](../../libs/np-syslib/include/cultures/Helpers.h)
