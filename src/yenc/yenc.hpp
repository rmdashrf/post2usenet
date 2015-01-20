#ifndef YENC_YENC_HPP_
#define YENC_YENC_HPP_

#include <cstddef>
#include <string>

namespace p2u
{
    namespace yenc
    {
        class encoder
        {
            private:
                size_t m_linelength;

                bool needs_escaping(unsigned char c, size_t linepos);
            public:
                encoder(size_t linelength);

                /**
                 * Encodes the next yEnc line from the bytes specified
                 * by [first,last)
                 *
                 * Returns an iterator to the next byte to escape. The caller
                 * should keep calling this function until there are no more
                 * bytes to escape.
                 */
                template <class InputIterator, class OutputIterator>
                InputIterator operator()(InputIterator first,
                                  InputIterator last,
                                  OutputIterator out)
                {
                    size_t linepos = 0;
                    InputIterator it = first;
                    for (; it != last; ++it)
                    {
                        unsigned char byte =
                            static_cast<unsigned char>(*it) + 42;

                        if (needs_escaping(byte, linepos))
                        {
                            *out++ = '=';
                            *out++ = (byte + 64); // per the yenc spec
                            linepos += 2;
                        } else {
                            *out++ = byte;
                            ++linepos;
                        }

                        if (linepos >= m_linelength) {
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

                size_t get_line_length() const;
        };
    }
}
#endif
