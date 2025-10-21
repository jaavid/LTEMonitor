#define BOOST_TEST_MODULE AngularServer_Test
#define BOOST_AUTO_TEST_MAIN

#include <boost/test/unit_test.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/system/system_error.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AngularServer.hpp"
#include "HuaweiClient.hpp"

namespace http = boost::beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using boost::property_tree::ptree;

struct mockClient : public RouterClient {
    std::string serverValue;
    std::string userValue;
    std::string passwordValue;
    std::atomic<int> active_calls{0};
    std::atomic<int> max_active{0};
    std::chrono::milliseconds queryDelay{0};
    bool calLogin{false};
    bool connected{false};
    bool loggedIn{false};

    mockClient() = default;
    mockClient(const std::string& server, const std::string& user, const std::string& password)
        : serverValue(server), userValue(user), passwordValue(password) {}

    void login(const std::string& server, const std::string& user, const std::string& password) override {
        calLogin = true;
        serverValue = server;
        userValue = user;
        passwordValue = password;
    }

    bool isConnected() const override { return connected; }
    bool isLoggedIn() const override { return loggedIn; }
    std::string getUserName() override { return userValue; }
    std::string getPassword() override { return passwordValue; }
    std::string getServer() override { return serverValue; }

    std::string Query(const std::string& method, const std::string& path, const std::string& data) override {
        (void)method;
        (void)path;
        (void)data;
        const int current = active_calls.fetch_add(1) + 1;
        int expected = max_active.load();
        while (current > expected && !max_active.compare_exchange_weak(expected, current)) {
        }
        if (queryDelay.count() > 0) {
            std::this_thread::sleep_for(queryDelay);
        }
        active_calls.fetch_sub(1);
        return "{ \"fakeresponse\":\"ok\"}";
    }
};

namespace {
std::atomic<unsigned short> next_port{19000};

http::response<http::string_body> perform_request(unsigned short port, http::verb method, const std::string& target, const std::string& body = std::string()) {
    net::io_context ioc;
    tcp::resolver resolver{ioc};
    auto const results = resolver.resolve("127.0.0.1", std::to_string(port));

    boost::beast::tcp_stream stream{ioc};
    boost::system::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        stream.connect(results, ec);
        if (!ec) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (ec) {
        throw boost::system::system_error(ec);
    }

    http::request<http::string_body> request{method, target, 11};
    request.set(http::field::host, "127.0.0.1");
    if (!body.empty() || method == http::verb::put || method == http::verb::post) {
        request.set(http::field::content_type, "application/json; charset=UTF-8");
        request.body() = body;
        request.prepare_payload();
    }

    http::write(stream, request);

    boost::beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return response;
}
} // namespace

struct Fixture {
    mockClient mc;
    unsigned short port{next_port.fetch_add(1)};
    AngularServer server;

    Fixture() : server(mc, port) {}
    ~Fixture() { server.stop(); }

    http::response<http::string_body> request(http::verb method, const std::string& target, const std::string& body = std::string()) {
        return perform_request(port, method, target, body);
    }
};

struct HuaweiFixture {
    unsigned short port{next_port.fetch_add(1)};
    HuaweiClient hc{"fakehost", "fakeuser", "fakepasswd"};
    AngularServer server;

    HuaweiFixture() : server(hc, port) {}
    ~HuaweiFixture() { server.stop(); }

    http::response<http::string_body> request(http::verb method, const std::string& target, const std::string& body = std::string()) {
        return perform_request(port, method, target, body);
    }
};

BOOST_FIXTURE_TEST_CASE(serving_functions, Fixture) {
    auto response = request(http::verb::get, "/config");
    ptree pt;
    std::istringstream config_stream(response.body());
    read_json(config_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("config.server", ""), "");
    BOOST_CHECK_EQUAL(pt.get("config.user", ""), "");
    BOOST_CHECK_EQUAL(pt.get("config.password", ""), "");
    BOOST_CHECK_EQUAL(mc.calLogin, false);

    response = request(http::verb::get, "/api/test");
    std::istringstream api_stream(response.body());
    read_json(api_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("fakeresponse", ""), "ok");
}

BOOST_FIXTURE_TEST_CASE(basic_init, HuaweiFixture) {
    auto response = request(http::verb::get, "/config");
    ptree pt;
    std::istringstream config_stream(response.body());
    read_json(config_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("config.server", ""), "fakehost");
    BOOST_CHECK_EQUAL(pt.get("config.user", ""), "fakeuser");
    BOOST_CHECK_EQUAL(pt.get("config.password", ""), "fakepasswd");
}

BOOST_FIXTURE_TEST_CASE(serving_data, HuaweiFixture) {
    ptree pt;

    auto response = request(http::verb::get, "/config");
    std::istringstream config_stream(response.body());
    read_json(config_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("config.server", ""), "fakehost");
    BOOST_CHECK_EQUAL(pt.get("config.user", ""), "fakeuser");
    BOOST_CHECK_EQUAL(pt.get("config.password", ""), "fakepasswd");

    response = request(http::verb::get, "/ping");
    std::istringstream ping_stream(response.body());
    read_json(ping_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("ping", ""), "ok");

    response = request(http::verb::get, "/status");
    std::istringstream status_stream(response.body());
    read_json(status_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("status.connected", ""), "false");
    BOOST_CHECK_EQUAL(pt.get("status.loggedin", ""), "false");

    ptree outpt;
    outpt.put("config.server", "new-server");
    outpt.put("config.user", "new-user");
    outpt.put("config.password", "new-password");
    std::ostringstream ss;
    write_json(ss, outpt);

    response = request(http::verb::put, "/config", ss.str());
    std::istringstream put_stream(response.body());
    read_json(put_stream, pt);
    BOOST_CHECK_EQUAL(pt.get("config.server", ""), "new-server");
    BOOST_CHECK_EQUAL(pt.get("config.user", ""), "new-user");
    BOOST_CHECK_EQUAL(pt.get("config.password", ""), "new-password");
}

BOOST_FIXTURE_TEST_CASE(concurrent_requests_are_serialized, Fixture) {
    mc.queryDelay = std::chrono::milliseconds(30);
    const std::size_t request_count = 8;
    std::vector<std::thread> threads;
    std::vector<std::string> responses;
    responses.resize(request_count);

    for (std::size_t i = 0; i < request_count; ++i) {
        threads.emplace_back([&, i]() {
            auto response = request(http::verb::get, "/api/test");
            responses[i] = response.body();
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (const auto& body : responses) {
        ptree pt;
        std::istringstream api_stream(body);
        read_json(api_stream, pt);
        BOOST_CHECK_EQUAL(pt.get("fakeresponse", ""), "ok");
    }

    BOOST_CHECK_EQUAL(mc.max_active.load(), 1);
}

BOOST_FIXTURE_TEST_CASE(graceful_shutdown_via_stop_route, Fixture) {
    auto response = request(http::verb::get, "/stop");
    BOOST_CHECK_EQUAL(response.result(), http::status::ok);
    BOOST_CHECK_EQUAL(response.body(), "stopping");

    // Allow the shutdown timer to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    bool connection_failed = false;
    try {
        (void)request(http::verb::get, "/ping");
    } catch (const boost::system::system_error&) {
        connection_failed = true;
    }
    BOOST_CHECK(connection_failed);
}
