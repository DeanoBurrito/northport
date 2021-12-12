#pragma once

namespace sl
{
    template<typename Iterator, typename T>
    Iterator Find(Iterator first, Iterator last, const T& value)
    {
        while (first != last)
        {
            if (*first == value)
                return first;
            ++first;
        }
        return last;
    }

    template<typename Iterator, typename UnaryPredicate>
    Iterator FindIf(Iterator first, Iterator last, UnaryPredicate p)
    {
        while (first != last)
        {
            if (p(first))
                return first;
            ++first;
        }
        return last;
    }

    template<typename Iterator, typename UnaryPredicate>
    Iterator FindIfNot(Iterator first, Iterator last, UnaryPredicate q)
    {
        while (first != last)
        {
            if (!q(first))
                return first;
            ++first;
        }
        return last;
    }
}