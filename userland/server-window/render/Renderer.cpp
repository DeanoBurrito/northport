#include <render/Renderer.h>
#include <formats/Qoi.h>
#include <SyscallFunctions.h>
#include <Logging.h>

namespace WindowServer
{
    void Renderer::LoadFile(const sl::String& name, GenericImage& image)
    {
        using namespace np::Syscall;
        
        auto maybeFileInfo = GetFileInfo(name);
        auto maybeFileHandle = OpenFile(name);
        if (!maybeFileInfo || !maybeFileHandle)
            np::Userland::Log("Renderer could not open file: %u", LogLevel::Error, name.C_Str());
        else
        {
            uint8_t* fileBuffer = new uint8_t[maybeFileInfo->fileSize];
            ReadFromFile(*maybeFileHandle, 0, 0, fileBuffer, maybeFileInfo->fileSize);

            const np::Graphics::Qoi::Header* header = sl::NativePtr(fileBuffer).As<np::Graphics::Qoi::Header>();

            if (sl::memcmp(header->magic, np::Graphics::Qoi::Magic, np::Graphics::Qoi::MagicLength) == 0)
            {
                auto maybeImg = np::Graphics::DecodeQoi({ fileBuffer, maybeFileInfo->fileSize });
                if (!maybeImg)
                    Log("Failed to load QOI image, no image returned.", LogLevel::Error);
                else
                    image = *maybeImg;
            }
            else
                Log("Requested file has incorrect file header.", LogLevel::Error);

            delete[] fileBuffer;
            CloseFile(*maybeFileHandle);
        }
    }
    
    void Renderer::DrawWindow(WindowDescriptor* window, const sl::UIntRect& rect)
    {
        using namespace np::Graphics;
        const DecorationConfig& decor = GetDecorConfig();
        
        if (sl::EnumHasFlag(window->controlFlags, WindowControlFlags::ShowTitlebar))
        {
            mainFb.DrawRect({ window->position.x - decor.borderWidth, window->position.y - decor.titleHeight, window->size.x + (2 * decor.borderWidth), decor.titleHeight}, Colours::DarkGrey, true);
            const size_t titleTop = window->position.y - decor.titleHeight;
            const size_t titleLeft = window->position.x + window->size.x + decor.borderWidth;
            mainFb.DrawImage(decor.closeImage, { titleLeft - decor.closeImage.Size().x, titleTop });
            mainFb.DrawImage(decor.maxImage, { titleLeft - decor.closeImage.Size().x - decor.maxImage.Size().x, titleTop });
            mainFb.DrawImage(decor.minImage, { titleLeft - decor.closeImage.Size().x - decor.maxImage.Size().x - decor.minImage.Size().x, titleTop });
        }

        if (!sl::EnumHasFlag(window->controlFlags, WindowControlFlags::Borderless))
        {
            mainFb.DrawRect({ window->position.x - decor.borderWidth, window->position.y, decor.borderWidth, window->size.y}, Colours::DarkGrey, true);
            mainFb.DrawRect({ window->position.x + window->size.x, window->position.y, decor.borderWidth, window->size.y}, Colours::DarkGrey, true);
            mainFb.DrawRect({ window->position.x - decor.borderWidth, window->position.y + window->size.y, window->size.x + (2 * decor.borderWidth), decor.borderWidth}, Colours::DarkGrey, true);
        }

        //then the background colour
        mainFb.DrawRect({ window->position.x, window->position.y, window->size.x, window->size.y }, Colours::DarkCyan, true);
    }

    sl::UIntRect Renderer::WindowBorderRect(const WindowDescriptor& window, const DecorationConfig& config)
    {
        sl::UIntRect border;
        border.left = window.position.x;
        border.top = window.position.y;
        border.width = window.size.x;
        border.height = window.size.y;
        
        if (!sl::EnumHasFlag(window.controlFlags, WindowControlFlags::Borderless))
        {
            border.left -= config.borderWidth;
            border.width += 2 * config.borderWidth;
            border.height += config.borderWidth;
            if (!sl::EnumHasFlag(window.controlFlags, WindowControlFlags::ShowTitlebar))
            {
                border.height += config.borderWidth;
                border.top -= config.borderWidth;
            }
        }
        if (sl::EnumHasFlag(window.controlFlags, WindowControlFlags::ShowTitlebar))
        {
            border.top -= config.titleHeight;
            border.height += config.titleHeight;
        }

        return border;
    }
    
    Renderer::Renderer()
    {
        screenFb = np::Graphics::LinearFramebuffer::Screen();
        mainFb = np::Graphics::LinearFramebuffer::Create(screenFb->Size().x, screenFb->Size().y, 32, screenFb->GetBufferFormat());
        mainFb.Clear(np::Graphics::Colours::Black);
        screenFb->Clear(np::Graphics::Colours::Black);
        decorConfig.borderWidth = 1;
        decorConfig.titleHeight = 32;

        LoadFile("/initdisk/icons/window-close.qoi", cursorImage);
        LoadFile("/initdisk/icons/window-close.qoi", decorConfig.closeImage);
        LoadFile("/initdisk/icons/window-min.qoi", decorConfig.minImage);
        LoadFile("/initdisk/icons/window-max.qoi", decorConfig.maxImage);

        // debugDrawLevel = RenderDebugDrawLevel::TextAndDamageRects;
    }

    void Renderer::Redraw(const sl::Vector<sl::UIntRect>& damageRects, const sl::Vector<WindowDescriptor*>& windows, sl::Vector2u cursor)
    {
        const sl::UIntRect cursorRect = { cursor.x, cursor.y, CursorSize().x, CursorSize().y };
        
        for (size_t rectIndex = 0; rectIndex < damageRects.Size(); rectIndex++)
        {
            //clear the damaged area
            mainFb.DrawRect(damageRects[rectIndex], 0x222222FF, true);

            //check if it overlaps any windows: render parts of the decorations if needed, and copy from the framebuffer if needed.
            for (size_t winIndex = 0; winIndex < windows.Size(); winIndex++)
            {
                if (windows[winIndex] == nullptr)
                    continue;

                WindowDescriptor* window = windows[winIndex];
                if (sl::EnumHasFlag(window->statusFlags, WindowStatusFlags::Minimized))
                    continue;
                
                if (damageRects[rectIndex].Intersects(WindowBorderRect(*window, GetDecorConfig())))
                    DrawWindow(window, damageRects[rectIndex]);
            }

            if (damageRects[rectIndex].Intersects(cursorRect))
                mainFb.DrawImage(cursorImage, cursor);

            if (debugDrawLevel == RenderDebugDrawLevel::DamageRectsOnly || debugDrawLevel == RenderDebugDrawLevel::TextAndDamageRects)
                mainFb.DrawRect(damageRects[rectIndex], np::Graphics::Colours::Red, false);
            
            screenFb->CopyFrom(mainFb, damageRects[rectIndex].TopLeft(), damageRects[rectIndex]);
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

    const DecorationConfig& Renderer::GetDecorConfig() const
    { return decorConfig; }

    ClickResult Renderer::TestWindowClick(const WindowDescriptor& window, sl::Vector2i cursor)
    {
        const sl::UIntRect border = WindowBorderRect(window, decorConfig);
        if (!border.Contains((sl::Vector2u)cursor))
            return ClickResult::Miss;
        
        if (window.Rect().Contains((sl::Vector2u)cursor))
            return ClickResult::Content;
        //TODO: test for controls (min/max/close)

        return ClickResult::Border;
    }

    void Renderer::AttachFramebuffer(WindowDescriptor*)
    {}

    void Renderer::DeteachFramebuffer(WindowDescriptor*)
    {}
}