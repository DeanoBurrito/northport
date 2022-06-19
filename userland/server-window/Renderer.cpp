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
            {
                auto maybeImg = np::Graphics::DecodeQoi({ fileBuffer, maybeFileInfo.Value()->fileSize });
                if (!maybeImg)
                    Log("Failed to load QOI image, no image returned.", LogLevel::Error);
                else
                    image = *maybeImg;
            }
            else
                Log("Requested file has incorrect file header.", LogLevel::Error);

            CloseFile(*maybeFileHandle);
        }
    }
    
    void Renderer::DrawWindow(WindowDescriptor* window)
    {
        using namespace np::Graphics;
        
        //first draw the titlebar + decorations
        mainFb.DrawRect({ window->position.x - windowBorderWidth, window->position.y - windowTitleHeight, window->size.x + (2 * windowBorderWidth), windowTitleHeight}, Colours::DarkGrey, true);
        const size_t titleTop = window->position.y - windowTitleHeight;
        const size_t titleLeft = window->position.x + window->size.x + windowBorderWidth;
        mainFb.DrawImage(closeImage, { titleLeft - closeImage.Size().x, titleTop });
        mainFb.DrawImage(maxImage, { titleLeft - closeImage.Size().x - maxImage.Size().x, titleTop });
        mainFb.DrawImage(minImage, { titleLeft - closeImage.Size().x - maxImage.Size().x - minImage.Size().x, titleTop });

        //next draw the window frame
        mainFb.DrawRect({ window->position.x - windowBorderWidth, window->position.y, windowBorderWidth, window->size.y}, Colours::DarkGrey, true);
        mainFb.DrawRect({ window->position.x + window->size.x, window->position.y, windowBorderWidth, window->size.y}, Colours::DarkGrey, true);
        mainFb.DrawRect({ window->position.x - windowBorderWidth, window->position.y + window->size.y, window->size.x + (2 * windowBorderWidth), windowBorderWidth}, Colours::DarkGrey, true);

        //then the background colour
        mainFb.DrawRect({ window->position.x, window->position.y, window->size.x, window->size.y }, Colours::DarkCyan, true);
    }
    
    Renderer::Renderer()
    {
        screenFb = np::Graphics::LinearFramebuffer::Screen();
        mainFb = np::Graphics::LinearFramebuffer::Create(screenFb->Size().x, screenFb->Size().y, 32, screenFb->GetBufferFormat());
        mainFb.Clear(np::Graphics::Colours::Black);
        screenFb->Clear(np::Graphics::Colours::Black);

        LoadFile("/initdisk/icons/cursor-default.qoi", cursorImage);
        LoadFile("/initdisk/icons/window-close.qoi", closeImage);
        LoadFile("/initdisk/icons/window-min.qoi", minImage);
        LoadFile("/initdisk/icons/window-max.qoi", maxImage);

        debugDrawLevel = RenderDebugDrawLevel::TextAndDamageRects;
    }

    void Renderer::Redraw(const sl::Vector<sl::UIntRect> damageRects, const sl::Vector<WindowDescriptor*> windows, sl::Vector2u cursor)
    {
        const sl::UIntRect cursorRect = { cursor.x, cursor.y, CursorSize().x, CursorSize().y };
        
        for (size_t rectIndex = 0; rectIndex < damageRects.Size(); rectIndex++)
        {
            mainFb.DrawRect(damageRects[rectIndex], np::Graphics::Colours::Black, true);

            for (size_t winIndex = 0; winIndex < windows.Size(); winIndex++)
            {
                WindowDescriptor* window = windows[winIndex];
                if (sl::EnumHasFlag(window->statusFlags, WindowStatusFlags::Minimized))
                    continue;
                
                if (damageRects[rectIndex].Intersects(window->BorderRect()))
                    DrawWindow(window);
            }

            if (damageRects[rectIndex].Intersects(cursorRect))
                mainFb.DrawImage(cursorImage, cursor);

            if (debugDrawLevel == RenderDebugDrawLevel::DamageRectsOnly || debugDrawLevel == RenderDebugDrawLevel::TextAndDamageRects)
                mainFb.DrawRect(damageRects[rectIndex], np::Graphics::Colours::Red, false);

            mainFb.SwapBuffers();
            screenFb->CopyFrom(mainFb, { damageRects[rectIndex].left, damageRects[rectIndex].top }, damageRects[rectIndex] );
        }

        if (debugDrawLevel == RenderDebugDrawLevel::TextOnly || debugDrawLevel == RenderDebugDrawLevel::TextAndDamageRects)
        {
            //TODO: draw debug text
        }
    }

    sl::Vector2u Renderer::Size() const
    { return mainFb.Size(); }

    sl::Vector2u Renderer::CursorSize() const
    { return cursorImage.Size(); }
}