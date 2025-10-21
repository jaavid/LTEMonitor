#pragma once

#include <functional>
#include <regex>
#include <string>

#include <boost/beast/http.hpp>

#include "RouterClient.hpp"
#include "server/http_server.hpp"

class AngularServer {
public:
    AngularServer() = delete;
    AngularServer(const AngularServer&) = delete;
    AngularServer& operator=(const AngularServer&) = delete;

    AngularServer(RouterClient& routerClient, int port, const std::function<void()>& afterStart = {});
    ~AngularServer() = default;

    void stop();

private:
    using Request = HttpServer::Request;
    using Response = HttpServer::Response;
    using HttpStatus = boost::beast::http::status;

    Response handleRequest(Request&& request);
    Response serveStatusGet(const Request& request);
    Response serveConfigGet(const Request& request);
    Response serveConfigPut(Request&& request);
    Response serveConfigOptions(const Request& request);
    Response serveApi(Request&& request);
    Response serveStopGet(const Request& request);
    Response serveResources(const Request& request);
    static Response servePingGet(const Request& request);

    static Response jsonResponse(const Request& request, std::string body, HttpStatus status = HttpStatus::ok);
    static Response textResponse(const Request& request, std::string body, HttpStatus status = HttpStatus::ok, const std::string& contentType = "text/plain; charset=UTF-8", bool keepAlive = true);
    static bool endsWith(const std::string& str, const std::string& suffix);

    RouterClient& rc;
    HttpServer server;
};
