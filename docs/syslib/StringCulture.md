# String Culture

The `StringCulture` class represents a unique combination of lexical rules, where the definitions of what is a letter or number may change. It also allows reuse between languages that share the same fundamental letters and number system.
The whole idea behind it, rather than just implementing these for ASCII, is it makes supporting other languages as first class citizens much easier down the road.

The default string culture is based on British English (the real english), and has definitons as such. Of course US english can use the same culture, because while some spellings may be different, the underlaying characters and definitions are the same.

An instance of a `StringCulture` implements many of usual string functions like `bool IsDigit()`, `bool IsAlpha()`, or `ToLower()`. These implementations use `int/int32_t/uint8_t[4]` as their character type, as there are plans for utf-8 support.

## Implementing a String Culture
It's very straight forward! Simply create a class that inherits from `class StringCulture`, and overload all the virtual methods.

This looks a little intimdating as there is a lot, but if your custom culture uses the arabic numbering system, you can reuse the existing number functions (they're templated in `StringCulture.h`).

Check how the default culture uses these for an example.

## Related source files:
- [syslib/include/StringCulture.h](../../syslib/include/StringCulture.h)
- [syslib/StringCulture.cpp](../../syslib/StringCulture.cpp)
