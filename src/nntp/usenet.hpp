#ifndef NNTP_USENET_HPP_
#define NNTP_USENET_HPP_
/**
 * This class defines a higher level API for interacting with "modern" usenet.
 *  (which usually involves multiple connections to potentially many different
 *   servers.... after all, who uses usenet for posting text anymore?)
 *
 * It manages connection lifetimes and contains a queue of messages to be sent.
 */

#include <boost/asio.hpp>
#include <list>
#include <memory>
#include <deque>
#include <vector>
#include <thread>
#include <condition_variable>
#include <unordered_map>
#include "connection.hpp"

namespace p2u
{
    namespace nntp
    {
        struct article;

        class usenet
        {
            private:
                struct conn_info_element
                {
                    std::unique_ptr<connection_info> info;
                    size_t num_connections;

                    conn_info_element(std::unique_ptr<connection_info> c, size_t n)
                        : info{std::move(c)}, num_connections{std::move(n)}
                    {

                    }
                };

                using connection_handle =
                    std::unique_ptr<p2u::nntp::connection>;

                using connection_handle_iterator =
                    std::list<connection_handle>::iterator;

                using queued_command = std::function<void(connection_handle_iterator)>;

                using post_event_callback = std::function<void(const std::shared_ptr<p2u::nntp::article>&)>;
                using on_finish_validate = std::function<void(const std::string& str)>;
                using on_finish_stat = std::function<void(const std::string&, stat_result)>;

                // Async-IO service
                boost::asio::io_service m_iosvc;
                std::unique_ptr<boost::asio::io_service::work> m_work;

                // The big fat mutex that we have to use that guards the ready
                // and busy lists along with the queue.
                //
                // We need one mutex that guards both of these seemingly
                // distinct pieces of code because of the following scenario
                //
                // Imagine the producer thread has one last message that it
                // wants to enqueue. Also imagine that we are only using a total
                // of *one* connection, and it is **just** about to become ready
                //
                // If we kept two separate locks, the following scenario could
                // occur:
                //
                // Enqueue(last_msg)
                // -----------------
                // [1] Grab list lock
                // [2] Check if there are any free connections
                // [3] Release list lock since there aren't
                // [4] grab queue lock
                // [5] Add last_msg to queue
                //
                // on_ready(connection)
                // --------------------
                // [1] Grab queue lock
                // [2] Check if there are any queued messages
                // [3] Release queue lock since there aren't
                // [4] Grab list lock
                // [5] Add connection to ready list
                //
                // If these events occured simultaneously, we will end up with
                // a connection that moves to the free list and a remaining item
                // in the queue. No work will progress.

                std::mutex m_bfm;
                // We maintain two lists that represent "ready" connections
                // (that are connected and are ready to start posting)
                // and "busy" connections (connections that are currently
                // in the process of connecting, authenticating, or posting)
                //
                // We use an std::list because it offers an O(1) splice operation.
                // Having a separate list for ready connections also allows us
                // to get the next available connection in O(1) time.
                std::list<connection_handle> m_ready;
                std::list<connection_handle> m_busy;


                // Queue of messages to be delivered.
                size_t m_maxsize;
                std::condition_variable m_queuecv;
                std::deque<queued_command> m_queue;


                // IO threadpool.
                size_t m_numthreads;
                std::vector<std::thread> m_iothreads;


                std::vector<conn_info_element> m_conninfo;

                post_event_callback m_slot_finish_post;
                post_event_callback m_slot_post_failed;
                on_finish_validate m_slot_finish_validate;
                on_finish_stat m_slot_finish_stat;


                void on_conn_becomes_ready(connection_handle_iterator conn);

                void on_post_finished(connection_handle_iterator conn,
                        const std::shared_ptr<p2u::nntp::article>& msg,
                        p2u::nntp::post_result post_result);

                void on_stat_finished(connection_handle_iterator conn,
                        const std::string& msgid,
                        p2u::nntp::stat_result stat_result);

                void on_connected(connection_handle_iterator conn,
                        p2u::nntp::connect_result result);

                void start_async_post(connection_handle_iterator conn,
                                     const std::shared_ptr<article>& msg);

                void start_async_stat(connection_handle_iterator conn,
                                     const std::string& msgid);

                void dispatch_or_queue(const queued_command& cmd, bool front=false);

                void discard_connection(connection_handle_iterator conn);
            public:
                usenet(size_t iothreads);
                usenet(size_t iothreads, size_t max_queue_size);
                usenet(const usenet& other) = delete;

                usenet& operator=(usenet& other) = delete;
                ~usenet();

                void add_connections(const connection_info& conninfo,
                                     size_t num_connections);

                /**
                 * Enqueues an article to be sent. If max queue size is non zero
                 * and the queue is == max queue size, this will block the
                 * caller
                 */
                void enqueue_post(const std::shared_ptr<article>& msg);
                void enqueue_stat(const std::string& msgid);
                void set_post_finished_callback(const post_event_callback& func);
                void set_post_failed_callback(const post_event_callback& func);
                void set_stat_finished_callback(const on_finish_stat& func);

                void start();
                void stop();
                void join();
        };
    }
}
#endif
