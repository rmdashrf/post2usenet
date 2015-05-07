#ifndef POSTSESSION_HPP_
#define POSTSESSION_HPP_

#include <boost/filesystem.hpp>
#include <iostream>
#include <fstream>
#include <memory>

#include "util/yencgenerator.hpp"

struct filepiece_key
{
    size_t file_index;
    size_t piece_index;

    bool operator<(const filepiece_key& rhs) const;
};

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
        size_t get_total_pieces() const;

        chunk get_chunk(size_t fileindex, size_t pieceindex);
        std::string get_usenet_subject(const std::string& subject, size_t fileIndex, size_t pieceIndex) const;
        static std::string get_usenet_message_id(const std::string& nonce, const std::string& domain, size_t fileIndex, size_t pieceIndex);

        // From a message like <nonce>.<fileindex>.<pieceindex>@<host>, retrieve fileindex and pieceindex
        static filepiece_key get_key_from_message_id(const std::string& msgid);

};
#endif
