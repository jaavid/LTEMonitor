#include "AngularServer.hpp"

#include "AngularResources.h"
#include "Version.h"

#include <boost/asio/steady_timer.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/version.hpp>

#include <chrono>
#include <sstream>

using boost::property_tree::ptree;
namespace http = boost::beast::http;

AngularServer::AngularServer(RouterClient& routerClient, int port, const std::function<void()>& afterStart)
    : rc(routerClient),
      server(static_cast<unsigned short>(port), [this](Request&& request) { return handleRequest(std::move(request)); }) {
    if (afterStart) {
        server.post(afterStart);
    }
}

void AngularServer::stop() {
    server.stop();
}

AngularServer::Response AngularServer::handleRequest(Request&& request) {
    const std::string target = request.target().to_string();
    const auto method = request.method();

    BOOST_LOG_TRIVIAL(info) << "incoming request [" << target << "] method " << request.method_string();

    if (target == "/ping" && method == http::verb::get) {
        return servePingGet(request);
    }
    if (target == "/status" && method == http::verb::get) {
        return serveStatusGet(request);
    }
    if (target == "/config") {
        if (method == http::verb::get) {
            return serveConfigGet(request);
        }
        if (method == http::verb::put) {
            return serveConfigPut(std::move(request));
        }
        if (method == http::verb::options) {
            return serveConfigOptions(request);
        }
    }
    static const std::regex apiRegex{"^/api/(.+)$"};
    if (std::regex_match(target, apiRegex) &&
        (method == http::verb::get || method == http::verb::put || method == http::verb::post || method == http::verb::delete_)) {
        return serveApi(std::move(request));
    }
    if (target == "/stop" && method == http::verb::get) {
        return serveStopGet(request);
    }
    if (method == http::verb::get) {
        return serveResources(request);
    }

    return textResponse(request, "", HttpStatus::not_found, "text/plain; charset=UTF-8", request.keep_alive());
}

AngularServer::Response AngularServer::serveStatusGet(const Request& request) {
    BOOST_LOG_TRIVIAL(info) << "sending status";
    ptree tree;
    tree.put("status.connected", rc.isConnected());
    tree.put("status.loggedin", rc.isLoggedIn());
    std::ostringstream ss;
    write_json(ss, tree);
    return jsonResponse(request, ss.str());
}

AngularServer::Response AngularServer::serveConfigGet(const Request& request) {
    BOOST_LOG_TRIVIAL(info) << "sending config";
    ptree tree;
    tree.put("config.server", rc.getServer());
    tree.put("config.user", rc.getUserName());
    tree.put("config.password", rc.getPassword());
    std::ostringstream versionStream;
    versionStream << lte_monitor_VERSION_MAJOR << "." << lte_monitor_VERSION_MINOR;
    tree.put("config.appVersion", versionStream.str());
    std::ostringstream boostVersionStream;
    boostVersionStream << BOOST_VERSION / 100000 << "." << BOOST_VERSION / 100 % 1000 << "." << BOOST_VERSION % 100;
    tree.put("config.boostVersion", boostVersionStream.str());

    std::ostringstream ss;
    write_json(ss, tree);
    return jsonResponse(request, ss.str());
}

AngularServer::Response AngularServer::serveConfigPut(Request&& request) {
    BOOST_LOG_TRIVIAL(info) << "setting new config";
    ptree tree;
    ptree body;
    std::istringstream iss(request.body());
    read_json(iss, body);
    rc.login(body.get<std::string>("config.server"), body.get<std::string>("config.user"), body.get<std::string>("config.password"));
    tree.put("config.server", rc.getServer());
    tree.put("config.user", rc.getUserName());
    tree.put("config.password", rc.getPassword());
    std::ostringstream ss;
    write_json(ss, tree);
    return jsonResponse(request, ss.str());
}

AngularServer::Response AngularServer::serveConfigOptions(const Request& request) {
    BOOST_LOG_TRIVIAL(info) << "sending config options";
    auto response = textResponse(request, "", HttpStatus::ok, "text/plain; charset=UTF-8", request.keep_alive());
    response.set(http::field::access_control_allow_methods, "POST, GET, OPTIONS, PUT");
    response.set(http::field::access_control_allow_origin, "*");
    response.set(http::field::access_control_allow_headers, "Content-Type, Authorization, X-Requested-With");
    return response;
}

AngularServer::Response AngularServer::serveApi(Request&& request) {
    BOOST_LOG_TRIVIAL(info) << "sending api " << request.target();
    const auto method = std::string(request.method_string());
    const auto body = request.body();
    const auto responseBody = rc.Query(method, request.target().to_string(), body);
    return jsonResponse(request, responseBody);
}

AngularServer::Response AngularServer::serveStopGet(const Request& request) {
    BOOST_LOG_TRIVIAL(info) << "stopping";
    auto response = textResponse(request, "stopping", HttpStatus::ok, "text/plain; charset=UTF-8", false);
    auto timer = std::make_shared<boost::asio::steady_timer>(server.strand(), std::chrono::milliseconds(50));
    timer->async_wait([this, timer](const boost::system::error_code&) {
        server.stop();
    });
    return response;
}

AngularServer::Response AngularServer::serveResources(const Request& request) {
    const std::string originalTarget = request.target().to_string();
    std::string url = originalTarget;
    if (!url.empty() && url.front() == '/') {
        url.erase(0, 1);
    }
    if (url.empty() || url == "router" || url == "console" || url == "config" || url == "signal") {
        url = "index.html";
    }
    BOOST_LOG_TRIVIAL(info) << "sending resources [" << originalTarget << "] resolved to [" << url << "]";

    auto found = ResourcesMap.find(url);
    if (found == ResourcesMap.end()) {
        return textResponse(request, "", HttpStatus::not_found, "text/plain; charset=UTF-8", request.keep_alive());
    }

    Response response{HttpStatus::ok, request.version()};
    response.keep_alive(request.keep_alive());
    if (endsWith(url, ".js")) {
        response.set(http::field::content_type, "application/javascript; charset=UTF-8");
    } else if (endsWith(url, ".png")) {
        response.set(http::field::content_type, "image/png; charset=UTF-8");
    } else if (endsWith(url, ".css")) {
        response.set(http::field::content_type, "text/css");
    } else if (endsWith(url, ".html")) {
        response.set(http::field::content_type, "text/html; charset=UTF-8");
    } else {
        response.set(http::field::content_type, "application/octet-stream");
    }

    std::string body(found->second.second, found->second.second + found->second.first);
    response.body() = std::move(body);
    response.prepare_payload();
    return response;
}

AngularServer::Response AngularServer::servePingGet(const Request& request) {
    BOOST_LOG_TRIVIAL(info) << "sending ping";
    ptree tree;
    tree.put("ping", "ok");
    std::ostringstream ss;
    write_json(ss, tree);
    return jsonResponse(request, ss.str());
}

AngularServer::Response AngularServer::jsonResponse(const Request& request, std::string body, HttpStatus status) {
    return textResponse(request, std::move(body), status, "application/json; charset=UTF-8", request.keep_alive());
}

AngularServer::Response AngularServer::textResponse(const Request& request, std::string body, HttpStatus status, const std::string& contentType, bool keepAlive) {
    Response response{status, request.version()};
    response.keep_alive(keepAlive && request.keep_alive());
    response.set(http::field::content_type, contentType);
    response.body() = std::move(body);
    response.prepare_payload();
    return response;
}

bool AngularServer::endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
