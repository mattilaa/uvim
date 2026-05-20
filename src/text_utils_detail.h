#pragma once

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace text_utils::detail
{

template <typename T>
using enable_string_view_compatible =
    std::enable_if_t<std::is_constructible_v<std::string_view, T>, int>;

class FindAllRange
{
public:
    class iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::size_t*;
        using reference = const std::size_t&;

        iterator() = default;

        iterator(std::string_view text, std::string_view needle,
                 std::size_t pos) noexcept
            : text(text), needle(needle), pos(pos)
        {
            findNext(pos);
        }

        reference operator*() const noexcept
        {
            return pos;
        }

        pointer operator->() const noexcept
        {
            return &pos;
        }

        iterator& operator++() noexcept
        {
            findNext(pos + step());
            return *this;
        }

        iterator operator++(int) noexcept
        {
            iterator old = *this;
            ++(*this);
            return old;
        }

        friend bool operator==(const iterator& a,
                               const iterator& b) noexcept
        {
            return a.pos == b.pos && a.text.data() == b.text.data() &&
                   a.text.size() == b.text.size();
        }

        friend bool operator!=(const iterator& a,
                               const iterator& b) noexcept
        {
            return !(a == b);
        }

        static constexpr std::size_t npos() noexcept
        {
            return static_cast<std::size_t>(-1);
        }

    private:
        std::size_t step() const noexcept
        {
            return needle.empty() ? 1 : needle.size();
        }

        void findNext(std::size_t start) noexcept
        {
            if(needle.empty() || start > text.size())
            {
                pos = npos();
                return;
            }
            pos = text.find(needle, start);
        }

        std::string_view text;
        std::string_view needle;
        std::size_t pos = npos();
    };

    FindAllRange(std::string_view text, std::string needle)
        : text(text), needle(std::move(needle))
    {
    }

    iterator begin() const noexcept
    {
        return iterator{text, needle, 0};
    }

    iterator end() const noexcept
    {
        return iterator{text, needle, iterator::npos()};
    }

private:
    std::string_view text;
    std::string needle;
};

class FindCursor
{
public:
    FindCursor(std::string_view text, std::string needle)
        : text(text), needle(std::move(needle))
    {
    }

    bool next(std::size_t& out) noexcept
    {
        if(needle.empty() || nextSearch > text.size())
            return false;

        const std::size_t found = text.find(needle, nextSearch);
        if(found == npos())
            return false;

        out = found;
        current = found;
        nextSearch = found + step();
        return true;
    }

    void resume_at(std::size_t pos) noexcept
    {
        nextSearch = pos;
    }

    std::size_t current_position() const noexcept
    {
        return current;
    }

private:
    static constexpr std::size_t npos() noexcept
    {
        return static_cast<std::size_t>(-1);
    }

    std::size_t step() const noexcept
    {
        return needle.empty() ? 1 : needle.size();
    }

    std::string_view text;
    std::string needle;
    std::size_t nextSearch = 0;
    std::size_t current = npos();
};

} // namespace text_utils::detail
