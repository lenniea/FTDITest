#include <windows.h>
#include "ParseHex.h"

/*
 *  ===== ParseHexDigit =====
 *  Parse a string of hex digits ('0123456789ABCDEF') into
 *  a 8/16-bit value.
 *
 *  Returns HEX_ERROR (-1) for error else valid number
 */
int ParseHexDigit(const char ch)
{
    int digit;

    if (ch >= '0' && ch <= '9')
        digit = (ch - '0');
    else if (ch >= 'a' && ch <= 'f')
        digit = (ch - 'a') + 10;
    else if (ch >= 'A' && ch <= 'F')
        digit = (ch - 'A') + 10;
    else
        digit = HEX_ERROR;
    return digit;
}

/*
 *  ===== ParseHexByte =====
 *  Parse a string of hex digits ('0123456789ABCDEF') into a 8-bit byte.
 *
 *  Returns HEX_ERROR (-1) for error else valid byte
 */
int ParseHexByte(const char *line)
{
    int lo,hi;

    hi = ParseHexDigit(line[0]);
    if (hi < 0)
        return hi;
    lo = ParseHexDigit(line[1]);
    if (lo < 0)
        return lo;
    return (hi << 4) | lo;
}

#define IsBlank(c)      (c == ' ' || c == '\t')

/*
 *  ===== ParseHexBuf =====
 *  Parse a ASCII Text buffer as hex bytes
 *
 *  Returns HEX_ERROR (-1) for error else number of bytes parsed
 */
int ParseHexBuf(BYTE* buffer, const TCHAR* szText)
{
    int i = 0;
    int count = 0;
    for (;;)
    {
        /* skip optional blanks */
        TCHAR ch = szText[i];
        if (IsBlank(ch))
        {
            ++i;
            continue;
        }
        /* if ';' skip rest of line */
        if (ch == ';')
        {
            do {
                ch = szText[++i];
            } while (ch != '\0' && ch != '\n' && ch != '\r');
        }
        /* skip optional blanks */
        ch = szText[i];
        if (IsBlank(ch))
        {
            ++i;
            continue;
        }
        if (ch == '\0')
            break;
        /* convert 2 characters to byte */
        buffer[count++] = ParseHexByte(szText + i);
        i += 2;
    }
    return count;
}
