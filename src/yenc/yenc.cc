#include "yenc.hpp"

bool p2u::yenc::encoder::needs_escaping(unsigned char c, size_t linepos)
{
    // Damn yenc encoding rules.
    // NULL, LF, CR, = are critical - TAB/SPACE at the start/end of line are critical - '.' at the start of a line is (sort of) critical
    return c == 0x00 ||
           c == '\r' ||
           c == '\n' ||
           c == '='  ||
           (c == ' ' && ((linepos == 0 || linepos == m_linelength - 1))) ||
           (c == '\t' && ((linepos == 0 || linepos == m_linelength - 1))) ||
           (c == '.' && linepos == 0);
}

p2u::yenc::encoder::encoder(size_t linelength)
    : m_linelength{linelength}
{

}
