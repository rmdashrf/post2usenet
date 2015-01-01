#ifndef NNTP_CONNECTINO_INFO_HPP_
#define NTTP_CONNECTION_INFO_HPP_

#include <cstdint>

namespace p2u
{
    namespace nntp
    {
        struct connection_info
        {
            std::string username;
            std::string password;
            std::string serveraddr;
            std::uint16_t port;
            bool tls;
        };
    }
}
#endif
