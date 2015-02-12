#include "yenc.hpp"

bool p2u::yenc::needs_escaping(unsigned char c, size_t linepos,
                               size_t linelength)
{
    // Damn yenc encoding rules.
    //
    // From GoPostStuff:
    // NULL, LF, CR, = are critical - TAB/SPACE at the start/end of line
    // are critical - '.' at the start of a line is (sort of) critical

    return c == 0x00 ||
           c == '\r' ||
           c == '\n' ||
           c == '='  ||
           (c == ' ' && ((linepos == 0 || linepos == linelength - 1))) ||
           (c == '\t' && ((linepos == 0 || linepos == linelength - 1))) ||
           (c == '.' && linepos == 0);
}
