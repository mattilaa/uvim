#pragma once

#include <string>
#include <string_view>

// Picker queries (Ctrl-P find file, Ctrl-A grep) may end in ":<line>":
//   "SomeFil:415"  -> pattern "SomeFil", open the chosen file at line 415
//   "SomeFil:"     -> pattern "SomeFil" (line suffix still being typed)
//   "needle\:415"  -> pattern "needle:415" (escaped, no line jump)
//   "std::"        -> pattern "std::" (a double colon is never a suffix)
struct PickerQuery
{
    std::string pattern;
    int line = 0; // 1-based target line; meaningful only when hasLine
    bool hasLine = false;
};

inline PickerQuery parsePickerQuery(std::string_view query)
{
    PickerQuery parsed;
    if((query.starts_with("re:") || query.starts_with("fz:")) &&
       query.rfind(':') == 2)
    {
        parsed.pattern = std::string(query);
        return parsed;
    }
    size_t digits = 0;
    while(digits < query.size() &&
          query[query.size() - 1 - digits] >= '0' &&
          query[query.size() - 1 - digits] <= '9')
        ++digits;

    const bool colonBeforeDigits = query.size() > digits &&
                                   query[query.size() - 1 - digits] == ':';
    if(!colonBeforeDigits)
    {
        parsed.pattern = std::string(query);
        return parsed;
    }
    if(digits == 0)
    {
        // A lone trailing ':' starts a line suffix: don't search for it, so
        // typing ":415" never re-runs the search or loses the selection.
        const size_t colon = query.size() - 1;
        const char before = colon > 0 ? query[colon - 1] : '\0';
        if(before == ':' || before == '\\')
            parsed.pattern = std::string(query); // "std::", escaped "\:"
        else
            parsed.pattern = std::string(query.substr(0, colon));
        if(before == '\\')
            parsed.pattern.erase(colon - 1, 1);
        return parsed;
    }

    const size_t colon = query.size() - 1 - digits;
    if(colon > 0 && query[colon - 1] == '\\')
    {
        // Escaped: search the text literally, minus the backslash.
        parsed.pattern = std::string(query.substr(0, colon - 1));
        parsed.pattern += query.substr(colon);
        return parsed;
    }

    parsed.pattern = std::string(query.substr(0, colon));
    long long line = 0;
    for(char c : query.substr(colon + 1))
    {
        line = line * 10 + (c - '0');
        if(line > 1'000'000'000)
            break; // far past any real file; still "not found"
    }
    parsed.line = static_cast<int>(line);
    parsed.hasLine = true;
    return parsed;
}
