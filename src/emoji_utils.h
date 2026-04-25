#pragma once

constexpr int emojiCStringLen(const char* s)
{
    int len = 0;
    while(s[len] != '\0')
        ++len;
    return len;
}

constexpr int emojiGlyphWidth(const char* s)
{
    int w = 0;
    for(int i = 0; s[i] != '\0';)
    {
        unsigned char c = (unsigned char)s[i];
        if(c < 0x80)
        {
            w += 1;
            i += 1;
            continue;
        }

        int codepoint = 0;
        int len = 1;
        if((c & 0xE0) == 0xC0 && s[i + 1] != '\0')
        {
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && s[i + 1] != '\0' && s[i + 2] != '\0')
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)s[i + 1] & 0x3F) << 6) |
                        ((unsigned char)s[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && s[i + 1] != '\0' && s[i + 2] != '\0' &&
                s[i + 3] != '\0')
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)s[i + 1] & 0x3F) << 12) |
                        (((unsigned char)s[i + 2] & 0x3F) << 6) |
                        ((unsigned char)s[i + 3] & 0x3F);
            len = 4;
        }
        else
        {
            codepoint = c;
            len = 1;
        }

        if(codepoint != 0xFE0F && codepoint != 0xFE0E && codepoint != 0x200D)
            w += 2;
        i += len;
    }
    return w;
}
