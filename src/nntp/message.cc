#include "message.hpp"
#include <boost/algorithm/string/join.hpp>


void p2u::nntp::header::write_to(std::ostream& stream) const
{
    stream << "From: " << from << "\r\n";
    stream << "Newsgroups: " << boost::algorithm::join(newsgroups, ",") << "\r\n";
    stream << "Subject: " << subject << "\r\n";

    for (const auto& element : additional)
    {
        stream << element.field << ": " << element.value << "\r\n";
    }
}

p2u::nntp::article::article(header h)
    : m_header(std::move(h))
{

}

const p2u::nntp::header& p2u::nntp::article::get_header() const
{
    return m_header;
}

size_t p2u::nntp::article::get_payload_pieces() const
{
    return m_payload.size();
}

void p2u::nntp::article::add_payload_piece(payload_piece_type&& other)
{
    m_payload.emplace_back(std::move(other));
}


size_t p2u::nntp::article::get_payload_size() const
{
    size_t ret = 0;
    std::for_each(m_payload.begin(), m_payload.end(),
            [&ret](const payload_piece_type& piece)
            {
                ret += piece.size();
            });
    return ret;
}
