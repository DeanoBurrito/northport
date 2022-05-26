# Style Guide

Northport differs from the c++ standard library's code style, by quite a bit. It's more comparable to the C# standard library naming.

## Case Definitions
For clarity, the cases used are defined as follows:
- Snake case: All characters are lowercase, and words are separated by an underscore. This is mostly unused.
    - Example: the_brown_fox_jumped_over_the_lazy_dog.
- Macro case: Similar to snake case, except all characters are uppercase. 
    - Example: THE_BROWN_FOX_JUMPED_OVER_THE_LAZY_DOG.
- Pascal case: All characters are lowercase, except the first character in each word, which is uppercase. Words are not separated by anything. 
    - Example: TheBrownFoxJumpedOverTheLazyDog.
- Camel case: Similar to pascal case, except the leading word is not capitalized.
    - Example: theBrownFoxJumpedOverTheLazyDog.
- Kebab case: similar to snake case, uses the hyphen instead of underscore for word separation. All letters are lowercase.
    - Example: the-brown-dog-jumped-over-the-lazy-dog.

### A note On Acronyms
In the context of naming things, Acronyms can make things confusing. For example, the device known as the input/output advanced programmable interrupt controller is usually called the IOAPIC. However, this dosn't fit with the style of pascal case for file names. Therefore all acronyms are treated as a word. The above example would go from "IOAPIC" to either "IoApic" or "Ioapic".

### A Note On Shadowing
While the C++ standard does allow shadowing, in this project the shadowing of variables is not allowed.

----
## Names Outside of Code

### Header and Source Files
C++ header files are named using the '.h' extension, and source files are named with an extension of '.cpp'.

Header files should be in a separate, but mirrored, file tree. Usually under a directory named 'include'.

### Directory and File Names
All directory names should be named using kebab case, and file names should use pascal case to differentiate them.

----
## Names Inside of Code

### Variables
Variables at all scopes are named using camel case. The exception is variables that are constexpr (at any scope) may be named using macro case.

### Functions
Functions at all scopes are named using pascal case.

### Types (structs, classes, typedefs & usings)
All custom types and type aliases should be named using pascal case. The c-style suffix of '_t' to indicate a name is a type is not allowed.

### Pre-Processor Macros
Unsprisingly all macros should be named using macro case. All function-style macros should have their arguments given meaningful names.

### Namespaces
Namespaces should be kept short and meaningful, and follow the directory structure within reason. They are named using pascal case.

### Integer Types
Integer types (int vs int32_t) should be selected depending on purpose. Using a fixed width type should be done with intent, and to communicate the size of the integer is important. Any integer data that crosses the borders of the traditional program should be done using fixed sized integers. This includes MMIO, system calls, driver calls, networking and so on.

----
## Spacing

Code inside of functions should be grouped into blocks that perform single steps of the task to be performed. If a step is achieved in a single line, it may be grouped together with other steps that occur in a single line.
