#pragma once

#include <Core.hpp>
#include <Rects.hpp>

namespace Npk
{
    /* Describes a simple in-memory framebuffer. The mapping for the
     * framebuffer is expected to be managed externally
     */
    struct SimpleFramebuffer
    {
        void* opaque;
        uintptr_t vbase;
        size_t width;
        size_t height;
        size_t pitch;
        size_t bpp;
        uint8_t redShift;
        uint8_t greenShift;
        uint8_t blueShift;
        uint8_t redBits;
        uint8_t greenBits;
        uint8_t blueBits;
    };

    struct Colour
    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    };

    struct TextRendererConfig;
    struct TextRenderer;
    struct TextAdaptor;

    void AddSimpleFramebuffer(SimpleFramebuffer* fb);

    /* Creates a sharable config for text renderer instances to use.
     * This function returns nullptr on failure, otherwise a pointer to a valid
     * configuration instance.
     */
    TextRendererConfig* CreateTextRendererConfig(
        sl::Span<const uint8_t> fontData, sl::Vector2u fontSize, 
        sl::Span<const Colour> dim, sl::Span<const Colour> bright, 
        Colour foreground, Colour background);

    /* Creates an instance of a TextRenderer, which maintains a grid of
     * characters and formatting info, as well as a cursor. A renderer by itself
     * is useless, but will output any changes to the character grid to text
     * adaptors that are later attached to it.
     */
    TextRenderer* CreateTextRenderer(TextRendererConfig* conf, 
        sl::Vector2u size);

    /* Creates a text adaptor instance, which binds a text renderer to a simple
     * framebuffer. When a text renderer flushes its contents, it will write
     * them to any attached adaptors.
     * This function returns nullptr on failure, otherwise a pointer to a valid
     * `TextAdaptor` instance.
     */
    TextAdaptor* CreateTextAdaptor(TextRenderer* renderer, 
        SimpleFramebuffer* fb, sl::Vector2u margin);

    /* This function queues text to be rendered to any adaptors, and processes
     * some basic ANSI escape sequences. For rendered output to appear on
     * an adaptor, `FlushTextRenderer()` must be called. Additionally, the 
     * newline character `\n` will internally trigger a call to 
     *`FlushTextRenderer()`.
     */
    void WriteText(TextRenderer* engine, sl::StringSpan text);

    /* Renders and outputs any pending text for the given TextRenderer.
     */
    void FlushTextRenderer(TextRenderer* engine);

    /* Ensures renderer state is up to date and then triggers a full redraw on
     * all attached adaptors.
     */
    void FullRefreshTextRenderer(TextRenderer* engine);

    /* Similar to `FullRefreshTextRenderer()` but only triggers a redraw on a
     * specific adaptor.
     */
    void FullRefreshTextAdaptor(TextRenderer* engine, TextAdaptor* adaptor);
}
