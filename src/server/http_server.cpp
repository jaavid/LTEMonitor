#include "server/http_server.hpp"

#include <boost/beast/version.hpp>

#include <utility>

namespace {
using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, HttpServer& server)
        : stream_(std::move(socket)), server_(server) {}

    void run() { do_read(); }

private:
    void do_read() {
        request_ = {};
        http::async_read(stream_, buffer_, request_,
            [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes) {
                self->on_read(ec, bytes);
            });
    }

    void on_read(const boost::beast::error_code& ec, std::size_t) {
        if (ec == http::error::end_of_stream) {
            return do_close();
        }
        if (ec) {
            return;
        }

        auto version = request_.version();
        auto forwarded = std::move(request_);

        boost::asio::dispatch(server_.strand(), [self = shared_from_this(), version, req = std::move(forwarded)]() mutable {
            try {
                auto response = self->server_.handler_(std::move(req));
                response.version(version);
                self->response_.emplace(std::move(response));
            } catch (const std::exception& ex) {
                HttpServer::Response response{http::status::internal_server_error, version};
                response.keep_alive(false);
                response.set(http::field::content_type, "text/plain; charset=UTF-8");
                response.body() = ex.what();
                response.prepare_payload();
                self->response_.emplace(std::move(response));
            } catch (...) {
                HttpServer::Response response{http::status::internal_server_error, version};
                response.keep_alive(false);
                response.set(http::field::content_type, "text/plain; charset=UTF-8");
                response.body() = "Unknown error";
                response.prepare_payload();
                self->response_.emplace(std::move(response));
            }
            self->do_write();
        });
    }

    void do_write() {
        http::async_write(stream_, *response_,
            [self = shared_from_this()](boost::beast::error_code ec, std::size_t bytes) {
                self->on_write(ec, bytes);
            });
    }

    void on_write(const boost::beast::error_code& ec, std::size_t) {
        if (ec) {
            return;
        }
        bool close = response_->need_eof();
        response_.reset();
        if (close) {
            return do_close();
        }
        do_read();
    }

    void do_close() {
        boost::beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    }

    boost::beast::tcp_stream stream_;
    HttpServer& server_;
    boost::beast::flat_buffer buffer_;
    HttpServer::Request request_;
    std::optional<HttpServer::Response> response_;
};

} // namespace

HttpServer::HttpServer(unsigned short port, Handler handler)
    : acceptor_(io_context_),
      strand_(io_context_.get_executor()),
      handler_(std::move(handler)) {
    tcp::endpoint endpoint{tcp::v4(), port};
    boost::system::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        throw boost::system::system_error(ec);
    }

    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        throw boost::system::system_error(ec);
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        throw boost::system::system_error(ec);
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        throw boost::system::system_error(ec);
    }

    work_guard_.emplace(boost::asio::make_work_guard(io_context_));
    do_accept();

    io_thread_ = std::jthread([this](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            try {
                io_context_.run();
                break;
            } catch (...) {
                // swallow exceptions and continue until stop is requested
            }
        }
    });
}

HttpServer::~HttpServer() {
    stop();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

void HttpServer::stop() {
    bool expected = false;
    if (stopping_.compare_exchange_strong(expected, true)) {
        io_thread_.request_stop();
        post([this]() { handle_stop(); });
    }
}

void HttpServer::post(std::function<void()> fn) {
    boost::asio::post(strand_, std::move(fn));
}

boost::asio::strand<boost::asio::io_context::executor_type>& HttpServer::strand() {
    return strand_;
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        boost::asio::make_strand(io_context_),
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Session>(std::move(socket), *this)->run();
            }
            if (acceptor_.is_open()) {
                do_accept();
            }
        });
}

void HttpServer::handle_stop() {
    if (work_guard_) {
        work_guard_->reset();
        work_guard_.reset();
    }
    boost::system::error_code ec;
    acceptor_.cancel(ec);
    acceptor_.close(ec);
    io_context_.stop();
}

