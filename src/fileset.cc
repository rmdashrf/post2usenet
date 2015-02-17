#include "fileset.hpp"
#include "util/make_unique.hpp"
#include <sstream>

fileset::fileset(size_t article_size)
    : m_articlesize{article_size}
{

}

bool fileset::add_file(const boost::filesystem::path& p)
{
    if (!boost::filesystem::is_regular_file(p))
        return false;

    m_files.push_back(p);
    m_filehandles.emplace_back(std::make_unique<p2u::util::yencgenerator>(p, m_articlesize, 128));
    return true;
}

std::string fileset::get_file_name(size_t index) const
{
    return m_files.at(index).filename().generic_string();
}

size_t fileset::get_num_pieces(size_t index) const
{
    return m_filehandles.at(index)->num_parts();
}

size_t fileset::get_num_files() const
{
    return m_files.size();
}

fileset::chunk fileset::get_chunk(size_t fileindex, size_t pieceindex)
{
    return m_filehandles.at(fileindex)->get_part(pieceindex);
}

std::string fileset::get_usenet_subject(const std::string& subject, size_t fileIndex, size_t pieceIndex) const
{
    std::ostringstream stream;
    stream << subject << " [" << fileIndex+1 << "/" << get_num_files()
           << "] - \"" << get_file_name(fileIndex)
           << "\" yEnc (" << pieceIndex+1 << "/"
           << get_num_pieces(fileIndex) << ")";

    return stream.str();
}

std::string fileset::get_usenet_message_id(const std::string& nonce, size_t fileIndex, size_t pieceIndex)
{
    std::ostringstream stream;
    stream << "<" << nonce << "." << fileIndex << "." << pieceIndex << "@post2usenet>";
    return stream.str();
}

size_t fileset::get_total_pieces() const
{
    size_t ret = 0;
    for (const auto& file : m_filehandles)
    {
        ret += file->num_parts();
    }

    return ret;
}

filepiece_key fileset::get_key_from_message_id(const std::string& msgid)
{
    filepiece_key ret;

    size_t firstDot = msgid.find('.');
    assert(firstDot != std::string::npos);

    size_t secondDot = msgid.find('.', firstDot + 1);
    assert(secondDot != std::string::npos);

    size_t atsep = msgid.find('@', secondDot + 1);

    ret.file_index = std::stoul(msgid.substr(firstDot + 1, secondDot - (firstDot + 1)));
    ret.piece_index = std::stoul(msgid.substr(secondDot + 1, atsep - (secondDot + 1)));

    return ret;

}

bool filepiece_key::operator<(const filepiece_key& rhs) const
{
    if (file_index < rhs.file_index)
        return true;
    else if (file_index > rhs.file_index)
        return false;
    else
        return piece_index < rhs.piece_index;
}
