#ifndef __NET_H__
#define __NET_H__

#include "base.h"
#include "detail.h"
#include "stream.h"
#include "error.h"
#include "process.h"
#include "timers.h"

#ifdef __MINGW32__
#include <winsock.h>
#define EAFNOSUPPORT     WSAEAFNOSUPPORT
#define ECONNRESET       WSAECONNRESET
#define SET_ERRNO(x)  (SetLastError((DWORD)(x)))
#define IN6ADDRSZ       16
#define INADDRSZ         4
#define INT16SZ          2
inline int      inet_pton4(const char *src, unsigned char *dst) {
  static const char digits[] = "0123456789";
  int saw_digit, octets, ch;
  unsigned char tmp[INADDRSZ], *tp;

  saw_digit = 0;
  octets = 0;
  tp = tmp;
  *tp = 0;
  while((ch = *src++) != '\0') {
    const char *pch;

    if((pch = strchr(digits, ch)) != NULL) {
      unsigned int val = *tp * 10 + (unsigned int)(pch - digits);

      if(saw_digit && *tp == 0)
        return 0;
      if(val > 255)
        return 0;
      *tp = (unsigned char)val;
      if(! saw_digit) {
        if(++octets > 4)
          return 0;
        saw_digit = 1;
      }
    }
    else if(ch == '.' && saw_digit) {
      if(octets == 4)
        return 0;
      *++tp = 0;
      saw_digit = 0;
    }
    else
      return 0;
  }
  if(octets < 4)
    return 0;
  memcpy(dst, tmp, INADDRSZ);
  return 1;
}
#ifdef ENABLE_IPV6
inline int      inet_pton6(const char *src, unsigned char *dst) {
  static const char xdigits_l[] = "0123456789abcdef",
    xdigits_u[] = "0123456789ABCDEF";
  unsigned char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
  const char *xdigits, *curtok;
  int ch, saw_xdigit;
  size_t val;

  memset((tp = tmp), 0, IN6ADDRSZ);
  endp = tp + IN6ADDRSZ;
  colonp = NULL;
  /* Leading :: requires some special handling. */
  if(*src == ':')
    if(*++src != ':')
      return 0;
  curtok = src;
  saw_xdigit = 0;
  val = 0;
  while((ch = *src++) != '\0') {
    const char *pch;

    if((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
      pch = strchr((xdigits = xdigits_u), ch);
    if(pch != NULL) {
      val <<= 4;
      val |= (pch - xdigits);
      if(++saw_xdigit > 4)
        return 0;
      continue;
    }
    if(ch == ':') {
      curtok = src;
      if(!saw_xdigit) {
        if(colonp)
          return 0;
        colonp = tp;
        continue;
      }
      if(tp + INT16SZ > endp)
        return 0;
      *tp++ = (unsigned char) (val >> 8) & 0xff;
      *tp++ = (unsigned char) val & 0xff;
      saw_xdigit = 0;
      val = 0;
      continue;
    }
    if(ch == '.' && ((tp + INADDRSZ) <= endp) &&
        inet_pton4(curtok, tp) > 0) {
      tp += INADDRSZ;
      saw_xdigit = 0;
      break;    /* '\0' was seen by inet_pton4(). */
    }
    return 0;
  }
  if(saw_xdigit) {
    if(tp + INT16SZ > endp)
      return 0;
    *tp++ = (unsigned char) (val >> 8) & 0xff;
    *tp++ = (unsigned char) val & 0xff;
  }
  if(colonp != NULL) {
    /*
     * Since some memmove()'s erroneously fail to handle
     * overlapping regions, we'll do the shift by hand.
     */
    const ssize_t n = tp - colonp;
    ssize_t i;

    if(tp == endp)
      return (0);
    for(i = 1; i <= n; i++) {
      *(endp - i) = *(colonp + n - i);
      *(colonp + n - i) = 0;
    }
    tp = endp;
  }
  if(tp != endp)
    return 0;
  memcpy(dst, tmp, IN6ADDRSZ);
  return 1;
}
#endif

inline int inet_pton(int af, const char *src, void *dst) {
	switch (af) {
		case AF_INET:
			return (inet_pton4(src, (unsigned char *)dst));
#ifdef ENABLE_IPV6
		case AF_INET6:
			return (inet_pton6(src, (unsigned char *)dst));
#endif
		default:
			SET_ERRNO(EAFNOSUPPORT);
			return (-1);
	}
}
#else
#include <arpa/inet.h>
#endif

namespace native
{
    namespace net
    {
        /**
         *  @brief Gets the version of IP address format.
         *
         *  @param input    Input IP string.
         *
         *  @retval 4       Input string is a valid IPv4 address.
         *  @retval 6       Input string is a valid IPv6 address.
         *  @retval 0       Input string is not a valid IP address.
         */
        int isIP(const std::string& input)
        {
            if(input.empty()) return 0;

            unsigned char buf[sizeof(in6_addr)];

            if(inet_pton(AF_INET, input.c_str(), buf) == 1) return 4;
            else if(inet_pton(AF_INET6, input.c_str(), buf) == 1) return 6;

            return 0;
        }

        /**
         *  @brief Tests if the input string is valid IPv4 address.
         *
         *  @param input    Input IP string.
         *
         *  @retval true    Input string is a valid IPv4 address.
         *  @retval false    Input string is not a valid IPv4 address.
         */
        bool isIPv4(const std::string& input) { return isIP(input) == 4; }

        /**
         *  @brief Test if the input string is valid IPv6 address.
         *
         *  @param input    Input IP string.
         *
         *  @retval true    Input string is a valid IPv6 address.
         *  @retval false    Input string is not a valid IPv6 address.
         */
        bool isIPv6(const std::string& input) { return isIP(input) == 6; }

        struct SocketType
        {
            enum
            {
                IPv4,
                IPv6,
                Pipe,
                None
            };
        };

        class Server;

        class Socket : public Stream
        {
            friend class Server;

            static const int FLAG_GOT_EOF = 1 << 0;
            static const int FLAG_SHUTDOWN = 1 << 1;
            static const int FLAG_DESTROY_SOON = 1 << 2;
            static const int FLAG_SHUTDOWNQUED = 1 << 3;

        protected:
            Socket(detail::stream* handle, Server* server, bool allowHalfOpen)
                : Stream(handle, true, true)
                , socket_type_(SocketType::None)
                , stream_(nullptr)
                , server_(server)
                , flags_(0)
                , allow_half_open_(allowHalfOpen)
                , connecting_(false)
                , destroyed_(false)
                , bytes_written_(0)
                , bytes_read_(0)
                , pending_write_reqs_()
                , connect_queue_size_(0)
                , connect_queue_()
                , on_data_callback_()
                , on_end_callback_()
            {
                // register events
                registerEvent<event::connect>();
                registerEvent<event::data>();
                registerEvent<event::end>();
                registerEvent<event::timeout>();
                registerEvent<event::drain>();
                registerEvent<event::error>();
                registerEvent<event::close>();
                registerEvent<internal_destroy_event>(); // only for Server class

                // init socket
                init_socket(handle);
            }

            virtual ~Socket()
            {
            }

        public:
            virtual bool end(const Buffer& buffer)
            {
                if(connecting_ && ((flags_ & FLAG_SHUTDOWNQUED) == 0))
                {
                    if(buffer.size()) write(buffer);
                    writable(false);
                    flags_ |= FLAG_SHUTDOWNQUED;
                }

                if(!writable()) return false;
                writable(false);

                if(buffer.size()) write(buffer);

                if(!readable())
                {
                    destroySoon();
                }
                else
                {
                    flags_ |= FLAG_SHUTDOWN;

                    stream_->on_complete([&](detail::resval r){
                        assert(flags_ & FLAG_SHUTDOWN);
                        assert(!writable());

                        if(destroyed_) return;

                        if((flags_ & FLAG_GOT_EOF) || !readable())
                        {
                            destroy();
                        }
                    });
                    detail::resval rv = stream_->shutdown();
                    if(!rv)
                    {
                        destroy(Exception(rv));
                        return false;
                    }
                }

                return true;
            }

            virtual bool end(const std::string& str, const std::string& encoding=std::string(), int fd=-1)
            {
                // TODO: what about 'fd'?
                return end(Buffer(str, encoding));
            }
            virtual bool end()
            {
                return end(Buffer(nullptr));
            }

            // TODO: these overloads are inherited but not used for Socket.
            //virtual bool write(const Buffer& buffer) { assert(false); return false; } //-> virtual bool write(const Buffer& buffer, write_callback_type callback=nullptr)
            virtual bool write(const std::string& str, const std::string& encoding, int fd) { assert(false); return false; }

            // TODO: this is not inherited from Stream - a new overload.
            // callback is invoked after all data is written.
            virtual bool write(const Buffer& buffer, std::function<void()> callback=nullptr)
            {
                bytes_written_ += buffer.size();

                if(connecting_)
                {
                    connect_queue_size_ += buffer.size();
                    connect_queue_.push_back(std::make_pair(std::shared_ptr<Buffer>(new Buffer(buffer)), callback));
                    return false;
                }

                timers::active(this);

                if(!stream_) throw Exception("The socket is closed.");

                stream_->on_complete([=](detail::resval r) {
                    if(destroyed_) return;

                    if(!r)
                    {
                        destroy(r);
                        return;
                    }

                    timers::active(this);

                    pending_write_reqs_--;
                    if(pending_write_reqs_ == 0) emit<event::drain>();

                    if(callback) callback();

                    if((pending_write_reqs_== 0) && (flags_ & FLAG_DESTROY_SOON))
                    {
                        destroy();
                    }
                });
                detail::resval rv = stream_->write(buffer.base(), 0, buffer.size());
                if(!rv)
                {
                    destroy(rv);
                    return false;
                }

                pending_write_reqs_++;

                return stream_->write_queue_size() == 0;
            }

            virtual void destroy(Exception exception)
            {
                destroy_(true, exception);
            }

            virtual void destroy()
            {
                destroy_(false, Exception());
            }

            virtual void pause()
            {
                if(stream_)
                {
                    stream_->read_stop();
                    stream_->unref(); // See tcp::unref() does nothing.
                }
            }

            virtual void resume()
            {
                if(stream_)
                {
                    stream_->read_start();
                    stream_->ref(); // See tcp::unref() does nothing.
                }
            }

            virtual void destroySoon()
            {
                writable(false);
                flags_ |= FLAG_DESTROY_SOON;

                if(pending_write_reqs_ == 0)
                {
                    destroy();
                }
            }

            void setTimeout(unsigned int msecs, std::function<void()> callback=nullptr)
            {
                if(msecs > 0)
                {
                    timers::enroll(this, msecs);
                    timers::active(this);

                    if(callback) once<event::timeout>(callback);
                }
                else // msecs == 0
                {
                    timers::unenroll(this);
                }
            }

            bool setNoDelay()
            {
                if(socket_type_ == SocketType::IPv4 || socket_type_ == SocketType::IPv6)
                {
                    auto x = dynamic_cast<detail::tcp*>(stream_);
                    assert(x);

                    x->set_no_delay(true);
                    return true;
                }

                return false;
            }

            bool setKeepAlive(bool enable, unsigned int initialDelay=0)
            {
                if(socket_type_ == SocketType::IPv4 || socket_type_ == SocketType::IPv6)
                {
                    auto x = dynamic_cast<detail::tcp*>(stream_);
                    assert(x);

                    x->set_keepalive(enable, initialDelay/1000);
                    return true;
                }

                return false;
            }

            // TODO: only IPv4 supported.
            bool address(std::string& ip_or_pipe_name, int& port)
            {
                if(!stream_) return SocketType::None;

                switch(socket_type_)
                {
                case SocketType::IPv4:
                case SocketType::IPv6:
                    {
                        auto x = dynamic_cast<detail::tcp*>(stream_);
                        assert(x);

                        auto addr = x->get_sock_name();
                        if(addr)
                        {
                            ip_or_pipe_name = addr->ip;
                            port = addr->port;
                            return true;
                        }
                    }
                    return false;

                default:
                    return false;
                }
            }

            std::string readyState() const
            {
                if(connecting_) return "opening";
                else if(readable() && writable()) return "open";
                else if(readable() && !writable()) return "readOnly";
                else if(!readable() && writable()) return "writeOnly";
                else return "closed";
            }

            std::size_t bufferSize() const
            {
                if(stream_)
                {
                    return stream_->write_queue_size() + connect_queue_size_;
                }
                return 0;
            }

            std::string remoteAddress() const
            {
                if(socket_type_ == SocketType::IPv4 || socket_type_ == SocketType::IPv6)
                {
                    auto x = dynamic_cast<detail::tcp*>(stream_);
                    assert(x);

                    auto addr = x->get_peer_name();
                    if(addr) return addr->ip;
                }

                return std::string();
            }

            int remotePort() const
            {
                if(socket_type_ == SocketType::IPv4 || socket_type_ == SocketType::IPv6)
                {
                    auto x = dynamic_cast<detail::tcp*>(stream_);
                    assert(x);

                    auto addr = x->get_peer_name();
                    if(addr) return addr->port;
                }

                return 0;
            }

            // TODO: host name lookup (DNS) not supported
            bool connect(const std::string& ip_or_path, int port=0, std::function<void()> callback=nullptr)
            {
                auto ip = isIP(ip_or_path);

                // recreate socket handle if needed
                if(destroyed_ || !stream_)
                {
                    if(ip == 0)
                    {
                        // create pipe socket
                        init_socket(new detail::pipe);
                    }
                    else
                    {
                        // create TCP socket
                        init_socket(new detail::tcp);
                    }
                }

                timers::active(this);

                connecting_ = true;
                writable(true);

                // TODO: need more implementation from here

                return false;
            }

        private:
            void init_socket(detail::stream* stream)
            {
                stream_ = stream;
                if(stream_)
                {
                    // set on_read callback
                    stream_->on_read([&](const char* buffer, std::size_t offset, std::size_t length, detail::stream* pending, detail::resval r){
                        on_read(buffer, offset, length, r);
                    });

                    // identify socket type
                    if(stream_->uv_stream()->type == UV_TCP)
                    {
                        // TODO: cannot identify IPv6 socket type!
                        socket_type_ = SocketType::IPv4;
                    }
                    else if(stream_->uv_stream()->type == UV_NAMED_PIPE)
                    {
                        socket_type_ = SocketType::Pipe;
                    }
                }
            }

            void destroy_(bool failed, Exception exception)
            {
                if(destroyed_) return;

                connect_queue_cleanup();

                readable(false);
                writable(false);

                timers::unenroll(this);

                // Because of C++ declration order, we cannot call Server class member functions here.
                // Instead, Server handles this using delegate.
                if(server_)
                {
                    //server_->connections_--;
                    //server_->emitCloseIfDrained();
                    emit<internal_destroy_event>();
                }

                if(stream_)
                {
                    stream_->close();
                    stream_->on_read(nullptr);
                    stream_ = nullptr;
                }

                process::nextTick([&](){
                    if(failed) emit<event::error>(exception);

                    // TODO: node.js implementation pass one argument whether there was errors or not.
                    //emit<event::close>(failed);
                    emit<event::close>();
                });
            }

            void connect_queue_cleanup()
            {
                connecting_ = false;
                connect_queue_size_ = 0;
                connect_queue_.clear();
            }

            void on_read(const char* buffer, std::size_t offset, std::size_t length, detail::resval rv)
            {
                timers::active(this);

                std::size_t end_pos = offset + length;
                if(buffer)
                {
                    // TODO: implement decoding
                    // ..
                    if(haveListener<event::data>()) emit<event::data>(Buffer(&buffer[offset], length));

                    bytes_read_ += length;

                    if(on_data_callback_) on_data_callback_(buffer, offset, end_pos);
                }
                else
                {
                    if(rv.code() == UV_EOF)
                    {
                        readable(false);

                        assert(!(flags_ & FLAG_GOT_EOF));
                        flags_ |= FLAG_GOT_EOF;

                        if(!writable()) destroy();

                        if(!allow_half_open_) end();
                        if(haveListener<event::end>()) emit<event::end>();
                        if(on_end_callback_) on_end_callback_();
                    }
                    else if(rv.code() == ECONNRESET)
                    {
                        destroy();
                    }
                    else
                    {
                        destroy(Exception(rv));
                    }
                }
            }

        private:
            detail::stream* stream_;
            int socket_type_;

            int flags_;
            bool allow_half_open_;
            bool connecting_;
            bool destroyed_;

            Server* server_;

            std::size_t bytes_written_;
            std::size_t bytes_read_;
            std::size_t pending_write_reqs_;

            std::size_t connect_queue_size_;
            std::vector<std::pair<std::shared_ptr<Buffer>, std::function<void()>>> connect_queue_;

            // ondata: (buffer, offset, end)
            std::function<void(const char*, std::size_t, std::size_t)> on_data_callback_;

            std::function<void()> on_end_callback_;

            // only for Server class
            struct internal_destroy_event : public util::callback_def<> {};
        };

        class Server : public EventEmitter
        {
            friend Server* createServer(std::function<void(Server*)>, bool);

            Server(bool allowHalfOpen)
                : EventEmitter()
                , stream_(nullptr)
                , connections_(0)
                , max_connections_(0)
                , allow_half_open_(allowHalfOpen)
                , backlog_(128)
                , socket_type_(SocketType::None)
                , pipe_name_()
            {
                registerEvent<event::listening>();
                registerEvent<event::connection>();
                registerEvent<event::close>();
                registerEvent<event::error>();
            }

            virtual ~Server()
            {}

        public:
            // listen over TCP socket
            bool listen(int port, const std::string& host=std::string("0.0.0.0"), event::listening::callback_type listeningListener=nullptr)
            {
                return listen_(host, port, listeningListener);
            }

            // listen over unix-socket
            bool listen(const std::string& path, event::listening::callback_type listeningListener=nullptr)
            {
                return listen_(path, 0, listeningListener);
            }

            // TODO: implement Socket::pause() function.
            void pause(unsigned int msecs) {}

            void close()
            {
                if(!stream_)
                {
                    throw Exception("Server is not running (1).");
                }

                stream_->close();
                stream_ = nullptr;
                emitCloseIfDrained();
            }

            int address(std::string& ip_or_pipe_name, int& port)
            {
                if(!stream_) return SocketType::None;

                switch(socket_type_)
                {
                case SocketType::IPv4:
                    {
                        auto x = dynamic_cast<detail::tcp*>(stream_);
                        assert(x);

                        auto addr = x->get_sock_name();
                        if(addr)
                        {
                            ip_or_pipe_name = addr->ip;
                            port = addr->port;
                        }
                        assert(addr->is_ipv4);
                    }
                    break;

                case SocketType::IPv6:
                    {
                        auto x = dynamic_cast<detail::tcp*>(stream_);
                        assert(x);

                        auto addr = x->get_sock_name();
                        if(addr)
                        {
                            ip_or_pipe_name = addr->ip;
                            port = addr->port;
                        }
                        assert(!addr->is_ipv4);
                    }
                    break;

                case SocketType::Pipe:
                    {
                        ip_or_pipe_name = pipe_name_;
                    }
                    break;
                }

                return socket_type_;
            }

            std::size_t maxConnections() const { return max_connections_; }
            void maxConnections(std::size_t value) { max_connections_ = value; }

            std::size_t connections() const { return connections_; }

        protected:
            void emitCloseIfDrained()
            {
                if(stream_ || connections_) return;

                process::nextTick([&](){ emit<event::close>(); });
            }

            detail::stream* create_server_handle(const std::string& ip_or_pipe_name, int port)
            {
                detail::resval rv;
                if(isIPv4(ip_or_pipe_name))
                {
                    auto handle = new detail::tcp();
                    assert(handle);

                    if(!handle->bind(ip_or_pipe_name, port))
                    {
                        handle->close();
                        return nullptr;
                    }

                    socket_type_ = SocketType::IPv4;
                    return handle;
                }
                else if(isIPv6(ip_or_pipe_name))
                {
                    auto handle = new detail::tcp();
                    assert(handle);

                    if(!handle->bind6(ip_or_pipe_name, port))
                    {
                        handle->close();
                        return nullptr;
                    }

                    socket_type_ = SocketType::IPv6;
                    return handle;
                }
                else
                {
                    auto handle = new detail::pipe();
                    assert(handle);

                    if(!handle->bind(ip_or_pipe_name))
                    {
                        handle->close();
                        return nullptr;
                    }

                    socket_type_ = SocketType::Pipe;
                    pipe_name_ = ip_or_pipe_name;
                    return handle;
                }
            }

            bool listen_(const std::string& ip_or_pipe_name, int port, event::listening::callback_type listeningListener)
            {
                if(listeningListener) on<event::listening>(listeningListener);

                if(!stream_)
                {
                    stream_ = create_server_handle(ip_or_pipe_name, port);
                    if(!stream_)
                    {
                        process::nextTick([&](){
                            emit<event::error>(Exception("Failed to create a server socket (1)."));
                        });
                        return false;
                    }
                }

                detail::resval rv = stream_->listen(backlog_, [&](detail::stream* s, detail::resval r){
                    if(!r)
                    {
                        emit<event::error>(Exception(r, "Failed to accept client socket (1)."));
                    }
                    else
                    {
                        if(max_connections_ && connections_ > max_connections_)
                        {
                            // TODO: where is "error" event?
                            s->close(); // just close down?
                            return;
                        }

                        auto socket = new Socket(s, this, allow_half_open_);
                        assert(socket);

                        socket->once<Socket::internal_destroy_event>([&](){
                            connections_--;
                            emitCloseIfDrained();
                        });
                        socket->resume();

                        connections_++;
                        emit<event::connection>(socket);

                        socket->emit<event::connect>();
                    }
                });

                if(!rv)
                {
                    stream_->close();
                    stream_ = nullptr;
                    process::nextTick([&](){
                        emit<event::error>(Exception(rv, "Failed to initiate listening on server socket (1)."));
                    });
                    return false;
                }

                process::nextTick([&](){
                    emit<event::listening>();
                });
                return true;
            }

        public:
            detail::stream* stream_;
            std::size_t connections_;
            std::size_t max_connections_;
            bool allow_half_open_;
            int backlog_;
            int socket_type_;
            std::string pipe_name_;
        };

        Server* createServer(std::function<void(Server*)> callback, bool allowHalfOpen=false)
        {
            auto x = new Server(allowHalfOpen);
            assert(x);

            process::nextTick([=](){
                callback(x);
            });

            return x;
        }
    }
}

#endif
