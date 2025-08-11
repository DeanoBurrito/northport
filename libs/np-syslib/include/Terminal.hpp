#pragma once

#include <formats/Qoi.hpp>

namespace sl
{
    constexpr size_t TerminalColourCount = 8;
    constexpr size_t EscapeArgsCount = 8;

    struct TerminalConfig
    {
        uint32_t colours[TerminalColourCount];
        uint32_t brightColours[TerminalColourCount];
        uint32_t background; //TODO: replace usage of uint32_t with graphic primitive: pixel
        uint32_t foreground;

        size_t tabSize;
        size_t margin;
        size_t fontSpacing;

        void* fbBase;
        Vector2u fbSize;
        size_t fbStride;

        void* (*Alloc)(size_t bytes); //memory allocation function, returns nullptr on failure
    };

    class Terminal
    {
    private:
        struct Char
        {
            uint32_t fg;
            uint32_t bg;
            uint32_t value = 0;
        };

        struct QueuedOp
        {
            Vector2u pos;
            Char c;
        };

        /* The terminal works by queueing operations in ram, and then flushing them to the gpu
         * all at once, when ready. `queuedOpHead` and `queuedOpStore` are a bump allocator
         * for QueueOp structs, with a max of 1 queued op per terminal character, which is fine.
         * `currChars` is the what is currently drawn at terminal character, we keep a copy
         * ourselves to avoid reading the framebuffer - which may be accessed over several
         * transports and can be extremely slow.
         * `queuedOpsMap` is is an array of the same size as `currChars`, it represents what
         * changes we want to make to each terminal char. We accumulate changes here,
         * overwriting older ones for the same char, until Flush() is called.
         */
        size_t queuedOpHead;
        QueuedOp* queuedOpStore;
        QueuedOp** queuedOpsMap;
        Char* currChars;

        Span<bool> fontStore;
        uint32_t* fbBase;
        size_t fbStride;

        uint32_t colours[TerminalColourCount];
        uint32_t brightColours[TerminalColourCount];
        uint32_t defaultBg;
        uint32_t defaultFg;

        Vector2u cursorPos;
        Vector2u prevCursorPos;
        Vector2u size;
        Vector2u margin;
        Vector2u fontSize;
        uint32_t currFg;
        uint32_t currBg;
        bool cursorVisible;
        bool initialized = false;
        size_t tabSize;

        struct
        {
            unsigned escArgs[EscapeArgsCount];
            size_t escArgIndex;
            bool inEscape;
            bool inControlSeq;
            bool inEscArg;
        } parseState;

        void QueueOp(Char c, Vector2u pos);
        void DrawChar(Vector2u where, Char old, Char next);
        void DrawCursor();

        void QueueScroll();
        void QueueChar(uint32_t character);
        void HandleSgr();
        void HandleEraseScreen();
        void ParseControlSequence(const char c);
        void ProcessString(StringSpan str);

    public:
        bool Init(const TerminalConfig& config);
        void Deinit(void (*Free)(void* ptr, size_t size));

        void Write(StringSpan span, bool flush = false);
        void Flush();

        void EnableCursor(bool yes);
        void SetCursorPos(Vector2u where);
        Vector2u GetCursorPos() const;
    };
}
