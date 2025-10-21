#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <thread>

class HttpServer {
public:
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;
    using Handler = std::function<Response(Request&&)>;

    HttpServer(unsigned short port, Handler handler);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void stop();
    void post(std::function<void()> fn);
    boost::asio::strand<boost::asio::io_context::executor_type>& strand();

private:
    class Session;

    void do_accept();
    void handle_stop();

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    Handler handler_;
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    std::optional<WorkGuard> work_guard_;
    std::jthread io_thread_;
};
