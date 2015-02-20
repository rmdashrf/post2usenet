#include "usenet.hpp"
#include "message.hpp"
#include "connection_info.hpp"
#include "../util/make_unique.hpp"

p2u::nntp::usenet::usenet(size_t iothreads)
    : usenet{iothreads, 0}
{

}

p2u::nntp::usenet::usenet(size_t iothreads, size_t max_queue_size)
    : m_maxsize{max_queue_size}, m_numthreads{iothreads}
{

}

p2u::nntp::usenet::~usenet()
{

}

void p2u::nntp::usenet::start_async_post(connection_handle_iterator conn,
                                         const std::shared_ptr<article>& msg)
{
    auto& connection = *conn;
    connection->async_post(msg);
}

void p2u::nntp::usenet::start_async_stat(connection_handle_iterator conn,
                                         const std::string& msgid)
{
    auto& connection = *conn;
    connection->async_stat(msgid);
}

void p2u::nntp::usenet::enqueue_stat(const std::string& msgid)
{
    std::unique_lock<std::mutex> _lock{m_bfm};

    if (m_ready.size() > 0)
    {
        // Directly enqueue the task.

        // Get the iterator to the connection_handle that will enqueue the task
        auto it = m_ready.begin();

        // Transfer it into the busy list. This does not invalidate the iterator
        m_busy.splice(m_busy.begin(), m_ready, it);

        start_async_stat(it, msgid);
        return;
    }

    if (m_maxsize != 0 && m_queue.size() >= m_maxsize)
    {
        m_queuecv.wait(_lock,
                [this](){return m_queue.size() < m_maxsize;});
    }

    // Defer the start_async_post to a connection that will become ready.
    m_queue.push_back(std::bind(&p2u::nntp::usenet::start_async_stat, this, std::placeholders::_1, msgid));
}

void p2u::nntp::usenet::enqueue_post(const std::shared_ptr<p2u::nntp::article>& msg, bool bypass_wait)
{
    std::unique_lock<std::mutex> _lock{m_bfm};

    if (m_ready.size() > 0)
    {
        // Directly enqueue the task.

        // Get the iterator to the connection_handle that will enqueue the task
        auto it = m_ready.begin();

        // Transfer it into the busy list. This does not invalidate the iterator
        m_busy.splice(m_busy.begin(), m_ready, it);

        start_async_post(it, msg);
        return;
    }

    if (!bypass_wait)
    {
        if (m_maxsize != 0 && m_queue.size() >= m_maxsize)
        {
            m_queuecv.wait(_lock,
                    [this](){return m_queue.size() < m_maxsize;});
        }
    }
    // Defer the start_async_post to a connection that will become ready.
    m_queue.push_back(std::bind(&p2u::nntp::usenet::start_async_post, this, std::placeholders::_1, msg));
}

void p2u::nntp::usenet::on_conn_becomes_ready(connection_handle_iterator connit)
{
    std::lock_guard<std::mutex> _lock{m_bfm};

    if (m_queue.size() > 0)
    {
        // Queue is non empty, we can just queue the next command without having
        // to splice the iterator back into the ready list
        auto next_command = m_queue.front();
        m_queue.pop_front();

        // If we have a bounded queue, potentially producer(s) could be waiting
        if (m_maxsize != 0)
        {
            m_queuecv.notify_one();
        }

        next_command(connit);
    }
    else
    {
        // There is no work for us to do at the moment, Let's put ourself back
        // into the ready queue
        m_ready.splice(m_ready.end(), m_busy, connit);

        if (!m_work && m_queue.size() == 0)
        {
            auto& conn = *connit;
            conn->async_graceful_disconnect();
            m_busy.erase(connit);

            for (auto readyconnit = m_ready.begin(); readyconnit != m_ready.end();)
            {

                (*readyconnit)->async_graceful_disconnect();
                readyconnit = m_ready.erase(readyconnit);

            }
            std::cerr << "[INFO] Gracefully disconnecting connection. Number of connections left: " << m_busy.size() + m_ready.size() << std::endl;
        }
    }
}

void p2u::nntp::usenet::on_connected(connection_handle_iterator connit,
        p2u::nntp::connect_result result)
{
    if (result == p2u::nntp::connect_result::FATAL_CONNECT_ERROR)
    {
        std::cerr << "[ERROR] One of our connections could not connect. Hopefully somebody else can.." << std::endl;
        discard_connection(connit);
    }
    else if (result == p2u::nntp::connect_result::INVALID_CREDENTIALS)
    {
        std::cerr << "[ERROR] One of our connections reported invalid credentials. Hopefully there is someone else.." << std::endl;
        discard_connection(connit);
    }
    else
    {
        on_conn_becomes_ready(connit);
    }
}

void p2u::nntp::usenet::on_stat_finished(connection_handle_iterator conn,
                                         const std::string& msgid,
                                         p2u::nntp::stat_result stat_result)
{
    if (stat_result == p2u::nntp::stat_result::CONNECTION_ERROR)
    {
        std::cout << "[ERROR] Stat for " << msgid << " failed with connection error. Retrying.." << std::endl;
        std::lock_guard<std::mutex> _lock{m_bfm};
        m_queue.push_back(std::bind(&p2u::nntp::usenet::start_async_stat, this, std::placeholders::_1, msgid));

        (*conn)->close();
        (*conn)->async_connect();
    }
    else
    {
        on_conn_becomes_ready(conn);
        // Pass through to our observers
        if (m_slot_finish_stat)
        {
            m_slot_finish_stat(msgid, stat_result);
        }
    }
}

void p2u::nntp::usenet::dispatch_or_queue(const queued_command& cmd, bool front)
{
    std::lock_guard<std::mutex> _lock{m_bfm};

    if (m_ready.size() > 0)
    {
        // Get the iterator to the connection_handle that will enqueue the task
        auto it = m_ready.begin();

        // Transfer it into the busy list. This does not invalidate the iterator
        m_busy.splice(m_busy.begin(), m_ready, it);
        cmd(it);
    }
    else
    {
        if (front)
        {
            m_queue.push_front(cmd);
        }
        else
        {
            m_queue.push_back(cmd);
        }
    }
}

void p2u::nntp::usenet::discard_connection(connection_handle_iterator conn)
{
    std::lock_guard<std::mutex> _lock{m_bfm};
    m_busy.erase(conn);

    std::cerr << "[INFO] Number of connections left: " << m_busy.size() + m_ready.size() << std::endl;

    if (m_busy.size() == 0 && m_ready.size() == 0)
    {
        // We have no more connections to work with, so we can't do any work
        std::cerr << "[FATAL] No more connections to work with. " << std::endl;
        m_work.reset();
    }
}

void p2u::nntp::usenet::on_post_finished(connection_handle_iterator connit,
                                const std::shared_ptr<p2u::nntp::article>& msg,
                                p2u::nntp::post_result post_result)
{

    if (post_result == p2u::nntp::post_result::POSTING_NOT_PERMITTED)
    {
        std::cerr << "[ERROR] Posting not permitted on one of our connections. Disconnecting and hoping that someone else can do our job..." << std::endl;
        (*connit)->close();

        discard_connection(connit);
        dispatch_or_queue(std::bind(&p2u::nntp::usenet::start_async_post, this, std::placeholders::_1, msg));
    }
    else if (post_result == p2u::nntp::post_result::POST_FAILURE)
    {
        if (m_slot_post_failed)
        {
            m_slot_post_failed(msg);
        }
        on_conn_becomes_ready(connit);
    }
    else if (post_result ==
            p2u::nntp::post_result::POST_FAILURE_CONNECTION_ERROR)
    {

        std::cerr << "[ERROR] Fatal connection error occurred. Requeueing " << msg->get_header().subject << " and attempting to reconnect.. " << std::endl;
        (*connit)->close();
        (*connit)->async_connect();
        if (m_slot_post_failed)
        {
            m_slot_post_failed(msg);
        }
    }
    else
    {
        on_conn_becomes_ready(connit);
        if (m_slot_finish_post)
        {
            m_slot_finish_post(msg);
        }
    }
}

void p2u::nntp::usenet::join()
{
    for(auto& thread : m_iothreads)
    {
        thread.join();
    }
}

void p2u::nntp::usenet::stop()
{
    m_work.reset();
}

void p2u::nntp::usenet::start()
{
    m_work = std::make_unique<boost::asio::io_service::work>(m_iosvc);
    for (size_t i = 0; i < m_numthreads; ++i)
    {
        m_iothreads.emplace_back([this](){ m_iosvc.run(); });
    }
}

void p2u::nntp::usenet::set_post_failed_callback(const post_event_callback& func)
{
    m_slot_post_failed = func;
}

void p2u::nntp::usenet::set_post_finished_callback(const post_event_callback& func)
{
    m_slot_finish_post = func;
}

void p2u::nntp::usenet::set_stat_finished_callback(const on_finish_stat& func)
{
    m_slot_finish_stat = func;
}

size_t p2u::nntp::usenet::get_queue_size() const
{
    // Intentionally NOT guarding it with a mutex, see note in header
    return m_queue.size();
}


void p2u::nntp::usenet::add_connections(const p2u::nntp::connection_info& conninfo,
                                        size_t num_connections)
{
    m_conninfo.emplace_back(std::make_unique<p2u::nntp::connection_info>(conninfo), num_connections);
    auto& it = m_conninfo.back();

    std::lock_guard<std::mutex> _lock{m_bfm};
    for (size_t i = 0; i < num_connections; ++i)
    {
        m_busy.emplace_back(std::make_unique<p2u::nntp::connection>(m_iosvc,
                    *it.info));
        auto connit = std::prev(m_busy.end());
        (*connit)->set_post_handler(std::bind(&p2u::nntp::usenet::on_post_finished, this, connit, std::placeholders::_1, std::placeholders::_2));
        (*connit)->set_stat_handler(std::bind(&p2u::nntp::usenet::on_stat_finished, this, connit, std::placeholders::_1, std::placeholders::_2));
        (*connit)->set_connect_handler(std::bind(&p2u::nntp::usenet::on_connected, this, connit, std::placeholders::_1));
        (*connit)->async_connect();
    }
}
