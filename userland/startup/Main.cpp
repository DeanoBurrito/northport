#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>

void Main()
{
    using namespace np::Graphics;
    // LinearFramebuffer* framebuffer = LinearFramebuffer::Screen();
    // TerminalFramebuffer terminal = TerminalFramebuffer(framebuffer);

    // terminal.Print("Hello world from ring 3");

    while (1);
}

extern "C"
{
    void _start()
    { Main(); }
}
