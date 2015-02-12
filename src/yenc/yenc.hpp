#ifndef YENC_YENC_HPP_
#define YENC_YENC_HPP_

#include <cstddef>
#include <string>

namespace p2u
{
    namespace yenc
    {
        bool needs_escaping(unsigned char c, size_t linepos, size_t linelength);

        /**
         * Gets the next yenc line
         */
        template <class InputIterator, class OutputIterator>
        InputIterator encode_next_line(InputIterator first, InputIterator last,
                                       OutputIterator out, size_t linelength)
        {
            size_t linepos = 0;
            InputIterator it = first;
            for (; it != last; ++it)
            {
                unsigned char byte =
                    static_cast<unsigned char>(*it) + 42;

                if (needs_escaping(byte, linepos, linelength))
                {
                    *out++ = '=';
                    *out++ = (byte + 64); // per the yenc spec
                    linepos += 2;
                } else {
                    *out++ = byte;
                    ++linepos;
                }

                if (linepos >= linelength) {
                    // We are going to break out of the for loop, so
                    // it will need to be incremented
                    ++it;
                    break;
                }
            }

            // If we reach here, this means that we just encoded the
            // last block.
            // Write the \r\n and leave
            *out++ = '\r';
            *out++ = '\n';
            return it;
        }

        template <class InputIterator, class OutputIterator>
        void encode_block(InputIterator first, InputIterator last,
                                       OutputIterator out, size_t linelength)
        {
            for (; first != last;
                    first = encode_next_line(first, last, out, linelength));
        }
    }
}
#endif
