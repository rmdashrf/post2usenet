#include "message.hpp"
#include <boost/algorithm/string/join.hpp>


void p2u::nntp::header::write_to(std::ostream& stream)
{
    stream << "From: " << from << "\r\n";
    stream << "Newsgroups: " << boost::algorithm::join(newsgroups, ",") << "\r\n";
    stream << "Subject: " << subject << "\r\n";

    for (const auto& element : additional)
    {
        stream << element.field << ": " << element.value << "\r\n";
    }
}

