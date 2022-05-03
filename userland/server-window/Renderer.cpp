#include <Renderer.h>
#include <formats/Qoi.h>
#include <SyscallFunctions.h>

namespace WindowServer
{
    void Renderer::LoadFile(const sl::String& name, GenericImage& image)
    {
        using namespace np::Syscall;
        
        auto maybeFileInfo = GetFileInfo(name);
        auto maybeFileHandle = OpenFile(name);
        if (!maybeFileInfo || !maybeFileHandle)
            Log("Window renderer could not open requested file.", LogLevel::Error);
        else
        {
            uint8_t* fileBuffer = new uint8_t[maybeFileInfo.Value()->fileSize];
            ReadFromFile(*maybeFileHandle, 0, 0, fileBuffer, maybeFileInfo.Value()->fileSize);

            const np::Graphics::Qoi::Header* header = sl::NativePtr(fileBuffer).As<np::Graphics::Qoi::Header>();

            if (sl::memcmp(header->magic, np::Graphics::Qoi::Magic, np::Graphics::Qoi::MagicLength) == 0)
                image = np::Graphics::DecodeQoi(fileBuffer, maybeFileInfo.Value()->fileSize);
            else
                Log("requested file has incorrect file header.", LogLevel::Error);

            CloseFile(*maybeFileHandle);
        }
    }
    
    void Renderer::DrawWindow(WindowDescriptor* window)
    {
        using namespace np::Graphics;
        const size_t titlebarHeight = 32;
        const size_t borderThickness = 4;
        
        //first draw the titlebar + decorations
        outputFb->DrawRect({ window->position.x - borderThickness, window->position.y - titlebarHeight, window->size.x + (2 * borderThickness), titlebarHeight}, Colours::DarkGrey, true);
        const size_t titleTop = window->position.y - titlebarHeight;
        const size_t titleLeft = window->position.x + window->size.x + borderThickness;
        outputFb->DrawImage(closeImage, { titleLeft - closeImage.Size().x, titleTop });
        outputFb->DrawImage(maxImage, { titleLeft - closeImage.Size().x - maxImage.Size().x, titleTop });
        outputFb->DrawImage(minImage, { titleLeft - closeImage.Size().x - maxImage.Size().x - minImage.Size().x, titleTop });

        //next draw the window frame
        outputFb->DrawRect({ window->position.x - borderThickness, window->position.y, borderThickness, window->size.y}, Colours::DarkGrey, true);
        outputFb->DrawRect({ window->position.x + window->size.x, window->position.y, borderThickness, window->size.y}, Colours::DarkGrey, true);
        outputFb->DrawRect({ window->position.x - borderThickness, window->position.y + window->size.y, window->size.x + (2 * borderThickness), borderThickness}, Colours::DarkGrey, true);

        //then the background colour
        outputFb->DrawRect({ window->position.x, window->position.y, window->size.x, window->size.y }, Colours::DarkCyan, true);
    }

    void Renderer::DrawCursor(sl::Vector2u where)
    {

    }
    
    Renderer::Renderer()
    {
        outputFb = np::Graphics::LinearFramebuffer::Screen();
        outputFb->Clear(np::Graphics::Colours::Black);

        LoadFile("/initdisk/icons/cursor-default.qoi", cursorImage);
        LoadFile("/initdisk/icons/window-close.qoi", closeImage);
        LoadFile("/initdisk/icons/window-min.qoi", minImage);
        LoadFile("/initdisk/icons/window-max.qoi", maxImage);
    }

    void Renderer::DrawAll(const sl::Vector<WindowDescriptor*> windows)
    {
        for (size_t i = 0; i < windows.Size(); i++)
        {
            if (windows[i] == nullptr)
                continue;

            DrawWindow(windows[i]);
        }

        DrawCursor({100, 100});
    }
}