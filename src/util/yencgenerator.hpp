#ifndef UTIL_YENCGENERATOR_HPP_
#define UTIL_YENCGENERATOR_HPP_

#include <boost/filesystem.hpp>
#include <fstream>
#include <vector>
#include "../yenc/yenc.hpp"

namespace p2u
{
    namespace util
    {
        class yencgenerator
        {
            public:
                using payload_type = std::vector<char>;

            private:
                boost::filesystem::path m_filepath;
                size_t m_articlesize;
                size_t m_linesize;

                size_t m_numparts;
                size_t m_filesize;

                std::ifstream m_file;

            public:
                yencgenerator(const boost::filesystem::path& path,
                              size_t articlesize,
                              size_t linesize);

                size_t num_parts() const;
                payload_type get_part(size_t i);

        };
    }
}
#endif
