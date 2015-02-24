#ifndef NNTP_CONNECTION_INFO_HPP_
#define NNTP_CONNECTION_INFO_HPP_

#include <cstdint>
#include <boost/functional/hash.hpp>

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

        bool operator==(const connection_info& first,
                       const connection_info& second);
    }
}

namespace std
{
    template<>
    struct hash<p2u::nntp::connection_info>
    {
        size_t operator()(const p2u::nntp::connection_info& obj)
        {
            size_t seed = 0;
            boost::hash_combine(seed, obj.username);
            boost::hash_combine(seed, obj.password);
            boost::hash_combine(seed, obj.serveraddr);
            boost::hash_combine(seed, obj.port);
            boost::hash_combine(seed, obj.tls);
            return seed;
        }
    };
}
#endif
