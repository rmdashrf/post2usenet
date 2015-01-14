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

void p2u::nntp::usenet::enqueue(const std::shared_ptr<p2u::nntp::article>& msg)
{
    std::unique_lock<std::mutex> _lock{m_bfm};

    if (m_ready.size() > 0)
    {
        // Directly enqueue the task.

        // Get the iterator to the connection_handle that will enqueue the task
        auto it = m_ready.begin();

        // Transfer it into the busy list. This does not invalidate the iterator
        m_busy.splice(m_busy.begin(), m_ready, it);

        auto& connection = *it;
        connection->async_post(msg,
                std::bind(&p2u::nntp::usenet::on_post_finished, this,
                    it, msg, std::placeholders::_1));
        return;
    }

    if (m_maxsize != 0 && m_queue.size() == m_maxsize)
    {
        m_queuecv.wait(_lock,
                [this](){return m_queue.size() < m_maxsize;});
    }

    m_queue.push_back(msg);
}

void p2u::nntp::usenet::on_conn_becomes_ready(connection_handle_iterator connit)
{
    std::lock_guard<std::mutex> _lock{m_bfm};

    if (m_queue.size() > 0)
    {
        // Queue is non empty, we can just queue the next message without having
        // to splice the iterator back into the ready list
        auto next_msg = m_queue.front();
        m_queue.pop_front();

        // If we have a bounded queue, potentially producer(s) could be waiting
        if (m_maxsize != 0)
        {
            m_queuecv.notify_one();
        }

        (*connit)->async_post(next_msg,
                std::bind(&p2u::nntp::usenet::on_post_finished, this,
                    connit, next_msg, std::placeholders::_1));
    }
    else
    {
        // There is no work for us to do at the moment, Let's put ourself back
        // into the ready queue
        m_ready.splice(m_ready.end(), m_busy, connit);
    }
}

void p2u::nntp::usenet::on_connected(connection_handle_iterator connit,
        p2u::nntp::connect_result result)
{
    if (result == p2u::nntp::connect_result::FATAL_CONNECT_ERROR)
    {
        // TODO: Do something meaningful with the connect error.
        throw std::runtime_error{"Connect error wtf..."};
    }
    else if (result == p2u::nntp::connect_result::INVALID_CREDENTIALS)
    {
        throw std::runtime_error{"Invalid credentials"};
    }
    else
    {
        on_conn_becomes_ready(connit);
    }
}

void p2u::nntp::usenet::on_post_finished(connection_handle_iterator connit,
                                const std::shared_ptr<p2u::nntp::article>& msg,
                                p2u::nntp::post_result post_result)
{

    if (post_result == p2u::nntp::post_result::POSTING_NOT_PERMITTED)
    {
        // We can't post on this connection. For now, we will consider this
        // an unrecoverable error on the user's part. Abort the program here.
        throw std::runtime_error{"Posting not permitted on one of our connections"};
    }
    else if (post_result == p2u::nntp::post_result::POST_FAILURE)
    {
        // Retry the post
        (*connit)->async_post(msg,
                std::bind(&p2u::nntp::usenet::on_post_finished, this,
                    connit, msg, std::placeholders::_1));
    }
    else if (post_result ==
            p2u::nntp::post_result::POST_FAILURE_CONNECTION_ERROR)
    {

        std::lock_guard<std::mutex> _lock{m_bfm};
        // Yes, we could be violating the "max queue" size here, but at this
        // point it doesn't matter. So what if the queue size is slightly bigger?
        // We need to make sure that this post gets handled.
        //
        std::cout << "Fatal connection error occurred. Requeueing " << msg->article_header.subject << " and attempting to reconnect.. " << std::endl;
        m_queue.push_back(msg);
        (*connit)->close();

        (*connit)->async_connect(std::bind(&p2u::nntp::usenet::on_connected,
                    this, connit, std::placeholders::_1));
    }
    else
    {
        on_conn_becomes_ready(connit);
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
        (*connit)->async_connect(std::bind(&p2u::nntp::usenet::on_connected,
                    this, connit, std::placeholders::_1));
    }
}
