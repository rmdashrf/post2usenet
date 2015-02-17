#ifndef NNTP_MESSAGE_HPP_
#define NNTP_MESSAGE_HPP_

#include <vector>
#include <string>
#include <boost/noncopyable.hpp>
#include <memory>
#include <iostream>
#include <type_traits>
#include <algorithm>
#include <boost/asio/buffer.hpp>


namespace p2u
{
    namespace nntp
    {
        using message_payload = std::vector<char>;

        struct header
        {
            struct element {
                std::string field;
                std::string value;
            };

            std::string from;
            std::vector<std::string> newsgroups;
            std::string subject;
            std::string msgid;
            std::vector<element> additional;

            void write_to(std::ostream& stream) const;
        };

        static_assert(std::is_move_constructible<header>::value == 1,
                        "Header not move constructible");

        class article
        {
            public:
                using payload_piece_type = std::vector<char>;
            private:
                header m_header;
                std::vector<payload_piece_type> m_payload;
            public:
                article(header h);

                const header& get_header() const;

                /**
                 * We require ownership of the payload piece.
                 */
                void add_payload_piece(payload_piece_type&& other);

                size_t get_payload_pieces() const;

                size_t get_payload_size() const;

                /*
                 * Writes all payload pieces of the article.
                 */
                template <class OutputIterator>
                void write_payload_asio_buffers(OutputIterator it) const
                {
                    std::transform(m_payload.begin(), m_payload.end(),
                            it,
                            [](const payload_piece_type& piece)
                            {
                                return boost::asio::buffer(piece);
                            });
                }
        };

        static_assert(std::is_move_constructible<article>::value == 1,
                        "Article not move constructible");
    }
}
#endif
