#include <private/Debugger.hpp>

namespace Npk::Private
{
    enum class LexedType
    {
        Invalid,
        EndOfInput,
        Number,
        LeftParenthesis,
        RightParenthesis,
        Star,
        Plus,
        Minus,
        Slash,
        Percent,
        VariableName,
        Identifier,
        Equals,
    };

    enum class ValueType
    {
        None,
        Address,
        Variable,
    };

    struct Lexer
    {
        sl::StringSpan input;
        size_t head;
        size_t lastHead;
        size_t errorIndex;
        sl::StringSpan errorText;

        sl::StringSpan lastIdentifier;
        uintptr_t lastNumber;
    };

    struct ValueContext
    {
        ValueType type;
        union
        {
            uintptr_t addr;
            DebugVariable* var;
        };
    };

    static NpkStatus DoExpression(uintptr_t& store, Lexer& lex);

    static NpkStatus ArbitraryRead(uintptr_t& store, uintptr_t addr)
    {
        return NpkStatus::Unsupported; (void)store; (void)addr;
    }

    static NpkStatus ArbitaryWrite(uintptr_t value, uintptr_t addr)
    {
        return NpkStatus::Unsupported; (void)value; (void)addr;
    }

    static NpkStatus MakeLexer(Lexer& out, sl::StringSpan input)
    {
        if (input.Empty())
            return NpkStatus::InvalidArg;

        out.input = input;
        out.head = 0;
        out.lastHead = 0;
        out.errorIndex = 0;
        out.errorText = {};
        out.lastIdentifier = {};
        out.lastNumber = 0;

        return NpkStatus::Success;
    }

    static LexedType LexNext(Lexer& lexer)
    {
        while (lexer.head < lexer.input.Size())
        {
            const auto scan = lexer.input[lexer.head];

            if (scan != ' ' && scan != '\t')
                break;
            lexer.head++;
        }

        if (lexer.head >= lexer.input.Size())
            return LexedType::EndOfInput;

        auto IsIdent = [](char d, bool isStart) -> bool
        {
            if (d >= 'a' && d <= 'z')
                return true;
            if (d >= 'A' && d <= 'Z')
                return true;
            if (d == '_')
                return true;

            if (isStart)
                return false;

            if (d >= '0' && d <= '9')
                return true;

            return false;
        };

        char c = lexer.input[lexer.head];
        lexer.lastHead = lexer.head;

        if (c >= '0' && c <= '9')
        {
            size_t radix = 10;
            if (c == '0' && lexer.head + 1 < lexer.input.Size())
            {
                const char prefix = lexer.input[lexer.head + 1];
                if (prefix == 'x' || prefix == 'X')
                {
                    radix = 16;
                    lexer.head += 2;
                }
                else if (prefix == 'b' || prefix == 'B')
                {
                    radix = 2;
                    lexer.head += 2;
                }
            }

            lexer.lastNumber = 0;
            while (lexer.head < lexer.input.Size())
            {
                c = lexer.input[lexer.head];

                if (c >= '0' && c <= '9' && (size_t)(c - '0') < radix)
                {
                    lexer.lastNumber *= radix;
                    lexer.lastNumber += c - '0';
                }
                else if (radix > 10 && radix < 36 && c >= 'a' && c <= 'z'
                    && (size_t)(c - 'a' + 10) < radix)
                {
                    lexer.lastNumber *= radix;
                    lexer.lastNumber += (c - 'a' + 10);
                }
                else if (radix > 10 && radix < 36 && c >= 'A' && c <= 'Z'
                    && (size_t)(c - 'A' + 10) < radix)
                {
                    lexer.lastNumber *= radix;
                    lexer.lastNumber += (c - 'A' + 10);
                }
                else
                    break;

                lexer.head++;
            }

            return LexedType::Number;
        }

        auto ConsumeIdentifier = [=](Lexer& lex) -> void
        {
            const size_t begin = lex.head;

            while (lex.head < lex.input.Size())
            {
                if (!IsIdent(lex.input[lex.head], lex.head == begin))
                    break;

                lex.head++;
            }

            const size_t length = lex.head - begin;
            lex.lastIdentifier = lex.input.Subspan(begin, length);
        };

        if (c == DebugVarNamePrefix)
        {
            lexer.head++;
            ConsumeIdentifier(lexer);

            return LexedType::VariableName;
        }

        if (IsIdent(c, true))
        {
            ConsumeIdentifier(lexer);

            return LexedType::Identifier;
        }

        lexer.head++;
        switch (c)
        {
        case '(':
            return LexedType::LeftParenthesis;

        case ')':
            return LexedType::RightParenthesis;

        case '*':
            return LexedType::Star;

        case '+':
            return LexedType::Plus;

        case '-':
            return LexedType::Minus;

        case '/':
            return LexedType::Slash;

        case '%':
            return LexedType::Percent;

        case '=':
            return LexedType::Equals;

        default:
            return LexedType::Invalid;
        }
    }
    
    static void UndoPrevLex(Lexer& lexer)
    {
        lexer.head = lexer.lastHead;
    }

    static void SetLexError(Lexer& lexer, sl::StringSpan text)
    {
        lexer.errorIndex = lexer.head;
        lexer.errorText = text;
    }

    static NpkStatus DoTerminalExpression(uintptr_t& store, Lexer& lex,
        ValueContext& context)
    {
        const auto type = LexNext(lex);

        switch (type)
        {
        case LexedType::Invalid:
            return NpkStatus::InternalError;

        case LexedType::EndOfInput:
            return NpkStatus::NotAvailable;

        case LexedType::Number:
            store = lex.lastNumber;
            return NpkStatus::Success;

        case LexedType::VariableName:
            {
                DebugVariable* var;
                auto result = LookupDebugVariable(&var, lex.lastIdentifier);
                if (result != NpkStatus::Success)
                {
                    UndoPrevLex(lex);
                    SetLexError(lex, "invalid variable name");

                    return NpkStatus::InternalError;
                }

                result = ReadDebugVariable(&store, *var);
                if (result != NpkStatus::Success)
                    SetLexError(lex, "invalid variable data");

                context.var = var;
                context.type = ValueType::Variable;

                return result;
            }

        case LexedType::Identifier:
            //TODO: identifiers without the variable prefix are treated as
            //symbols. We'll need the kernel/debugger shared data state for this.
            return NpkStatus::Unsupported;

        case LexedType::LeftParenthesis:
            {
                auto result = DoExpression(store, lex);
                if (result != NpkStatus::Success)
                    return result;

                auto right = LexNext(lex);
                if (right!= LexedType::RightParenthesis)
                {
                    UndoPrevLex(lex);
                    SetLexError(lex, "expected closing paranthesis");

                    return NpkStatus::InternalError;
                }

                return NpkStatus::Success;
            }

        default:
            return NpkStatus::Unsupported;
        }
    }

    static NpkStatus DoUnaryExpression(uintptr_t& store, Lexer& lex, 
        ValueContext& context)
    {
        const auto op = LexNext(lex);
        if (op == LexedType::Minus)
        {
            uintptr_t rhs;
            auto result = DoUnaryExpression(rhs, lex, context);
            if (result != NpkStatus::Success)
                return result;

            store = -rhs;
            context.type = ValueType::None;

            return NpkStatus::Success;
        }
        else if (op == LexedType::Star)
        {
            uintptr_t addr;
            auto result = DoUnaryExpression(addr, lex, context);
            if (result != NpkStatus::Success)
                return result;

            result = ArbitraryRead(store, addr);
            if (result != NpkStatus::Success)
                return result;

            context.type = ValueType::Address;
            context.addr = addr;

            return NpkStatus::Success;
        }

        if (op != LexedType::Plus)
            UndoPrevLex(lex);

        return DoTerminalExpression(store, lex, context);
    }

    static NpkStatus DoBinaryExpression(uintptr_t& store, Lexer& lex,
        ValueContext& context)
    {
        return DoUnaryExpression(store, lex, context);
    }

    static NpkStatus DoAssignmentExpression(uintptr_t& store, Lexer& lex)
    {
        ValueContext context;
        context.type = ValueType::None;

        auto result = DoBinaryExpression(store, lex, context);
        if (result != NpkStatus::Success)
            return result;

        const auto op = LexNext(lex);
        if (op != LexedType::Equals)
        {
            UndoPrevLex(lex);

            return NpkStatus::Success;
        }

        uintptr_t value;
        result = DoExpression(value, lex);
        if (result != NpkStatus::Success)
            return result;

        switch (context.type)
        {
        case ValueType::None:
            SetLexError(lex, "only variables and addresses are assignable");
            return NpkStatus::NotWritable;

        case ValueType::Address:
            result = ArbitaryWrite(value, context.addr);
            break;

        case ValueType::Variable:
            result = WriteDebugVariable(*context.var, value, nullptr);
            break;
        }
        store = value;

        return result;
    }

    static NpkStatus DoExpression(uintptr_t& store, Lexer& lex)
    {
        return DoAssignmentExpression(store, lex);
    }

    NpkStatus ProcessExpression(uintptr_t& store, sl::StringSpan input)
    {
        if (input.Empty())
            return NpkStatus::InvalidArg;

        Lexer lexer {};
        auto result = MakeLexer(lexer, input);
        if (result != NpkStatus::Success)
            return result;

        return DoExpression(store, lexer);
    }
}
