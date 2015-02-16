#include <sstream>
#include <boost/crc.hpp>
#include "yencgenerator.hpp"

p2u::util::yencgenerator::yencgenerator(const boost::filesystem::path& path,
                                        size_t articlesize, size_t linesize)
    : m_filepath{path}, m_articlesize{articlesize}, m_linesize{linesize}
{
    if (!boost::filesystem::exists(path) ||
            !boost::filesystem::is_regular(path))
    {
        throw std::runtime_error{"Invalid filename supplied to yencgenerator"};
    }

    m_file.open(m_filepath.c_str(), std::ifstream::binary);
    if (!m_file.is_open())
    {
        throw std::runtime_error{"Could not open file passed to yencgenerator"};
    }

    m_filesize = boost::filesystem::file_size(m_filepath);
    m_numparts = m_filesize / m_articlesize;
    if (m_filesize % m_articlesize != 0) {
        ++m_numparts;
    }

}

size_t p2u::util::yencgenerator::num_parts() const
{
    return m_numparts;
}

p2u::util::yencgenerator::payload_type
p2u::util::yencgenerator::get_part(size_t partnumber)
{
    auto part_offset = partnumber * m_articlesize;
    m_file.seekg(part_offset);

    p2u::util::yencgenerator::payload_type ret;
    p2u::util::yencgenerator::payload_type buf;
    buf.reserve(m_articlesize);
    ret.reserve(m_articlesize * 1.5);

    boost::crc_32_type summer;

    m_file.read(&buf[0], m_articlesize);
    size_t bytes_read = m_file.gcount();

    std::ostringstream stream;
    stream << "=ybegin part=" << partnumber+1 << " total=" << num_parts()
        << " line=" << m_linesize
        << " size=" << m_filesize
        << " name=" << m_filepath.filename().generic_string() << "\r\n";
    std::string line = stream.str();
    ret.insert(ret.end(), line.begin(), line.end());

    stream.str(std::string{});
    stream << "=ypart begin=" << part_offset + 1 // Why the fuck would you make this 1 based index.
           << " end=" << part_offset + bytes_read << "\r\n";

    line = stream.str();

    ret.insert(ret.end(), line.begin(), line.end());



    auto data_end = buf.begin() + bytes_read;

    summer.process_bytes(&buf[0], bytes_read);

    p2u::yenc::encode_block(buf.begin(), data_end, std::back_inserter(ret),
            m_linesize);

    stream.str(std::string{});

    // Calculate CRC32 of the part
    uint32_t checksum = summer.checksum();

    stream << "=yend size=" << bytes_read << " part=" << partnumber+1
        << " pcrc32=" << std::hex << std::uppercase << checksum << "\r\n";
    line = stream.str();

    ret.insert(ret.end(), line.begin(), line.end());

    return ret;
}


