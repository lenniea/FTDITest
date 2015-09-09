#ifndef __PARSE_HEX_H__
#define __PARSE_HEX_H__

#ifdef __cplusplus
extern "C" {
#endif

#define HEX_ERROR       -1

int ParseHexDigit(const char ch);
int ParseHexByte(const TCHAR *line);
int ParseHexBuf(BYTE* buffer, const TCHAR* szText);

#ifdef __cplusplus
}
#endif

#endif /* __PARSE_HEX_H__ */
