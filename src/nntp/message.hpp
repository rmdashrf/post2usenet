#ifndef NNTP_MESSAGE_HPP_
#define NNTP_MESSAGE_HPP_

#include <vector>
#include <string>
#include <boost/noncopyable.hpp>
#include <memory>
#include <iostream>
#include <type_traits>


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
            std::vector<element> additional;

            void write_to(std::ostream& stream);
        };

        static_assert(std::is_move_constructible<header>::value == 1,
                        "Header not move constructible");

        struct article
        {
            header article_header;
            message_payload article_payload;

            ~article()
            {
                std::cout << "Article with subject; " << article_header.subject << " destroyed!" << std::endl;
            }
        };

        static_assert(std::is_move_constructible<article>::value == 1,
                        "Article not move constructible");
    }
}
#endif
