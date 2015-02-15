#ifndef POSTSESSION_HPP_
#define POSTSESSION_HPP_

#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <memory>

#include "util/yencgenerator.hpp"

class fileset
{
    private:
        std::vector<boost::filesystem::path> m_files;
        std::vector<std::unique_ptr<p2u::util::yencgenerator>> m_filehandles;
        size_t m_articlesize;

    public:
        using chunk = std::vector<char>;

        fileset(size_t article_size);

        bool add_file(const boost::filesystem::path& p);
        size_t get_num_pieces(size_t index) const;
        size_t get_num_files() const;
        std::string get_file_name(size_t index) const;

        chunk get_chunk(size_t fileindex, size_t pieceindex);

};
#endif
