#include <sstream>
#include <boost/crc.hpp>
#include "yencgenerator.hpp"

p2u::util::yencgenerator::yencgenerator(const boost::filesystem::path& path,
                                        size_t articlesize, size_t linesize)
    : m_filepath{path}, m_articlesize{articlesize}, m_encoder{linesize}
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
    m_numparts  = m_filesize / m_articlesize;
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
    std::array<char, 8192> buf;
    m_file.seekg(partnumber * m_articlesize);
    size_t bytes_total_processed = 0;

    p2u::util::yencgenerator::payload_type ret;
    ret.reserve(m_articlesize * 1.5);

    boost::crc_32_type summer;

    std::ostringstream stream;
    stream << "=ybegin part=" << partnumber+1 << " total=" << num_parts()
        << " line=" << m_encoder.get_line_length()
        << " size=" << m_filesize << " name=" << m_filepath.filename() << "\r\n";
    std::string line = stream.str();
    ret.insert(ret.end(), line.begin(), line.end());

    while (bytes_total_processed < m_articlesize)
    {
        size_t bytes_left = m_articlesize - bytes_total_processed;

        m_file.read(&buf[0], bytes_left > buf.size() ? buf.size() : bytes_left);

        size_t bytes_read = m_file.gcount();
        if (bytes_read == 0)
        {
            // Last piece
            break;
        }
        auto data_end = buf.begin() + bytes_read;

        summer.process_bytes(&buf[0], bytes_read);

        for (auto it = buf.begin(); it != data_end; it =
                m_encoder(it, data_end, std::back_inserter(ret)));

        bytes_total_processed += bytes_read;
    }

    stream.str(std::string{});

    // Calculate CRC32 of the part
    uint32_t checksum = summer.checksum();

    stream << "=yend size=" << bytes_total_processed << " part=" << partnumber+1
        << " pcrc32=" << std::hex << std::uppercase << checksum << "\r\n";
    line = stream.str();

    ret.insert(ret.end(), line.begin(), line.end());

    return ret;
}


