//#define BOOST_TEST_DYN_LINK //http://neyasystems.com/an-engineers-guide-to-unit-testing-cmake-and-boost-unit-tests/
#define BOOST_TEST_MODULE HuaweiClient_Test
#include <boost/test/unit_test.hpp>
#include "Simple-Web-Server/client_http.hpp"
#include "Simple-Web-Server/server_http.hpp"
#include "Simple-Web-Server/crypto.hpp"
#include "HuaweiClient.hpp"
#include "src/security/credentials.hpp"
#include <boost/log/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/core/null_deleter.hpp>
#include <sstream>
#include <thread>
#include <chrono>

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;

struct LogCapture {
        using text_sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
        boost::shared_ptr<text_sink> sink;
        std::ostringstream stream;

        LogCapture(){
                sink = boost::make_shared<text_sink>();
                sink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&stream, boost::null_deleter()));
                sink->locked_backend()->auto_flush(true);
                boost::log::core::get()->add_sink(sink);
        }

        ~LogCapture(){
                boost::log::core::get()->remove_sink(sink);
                sink->flush();
        }

        std::string str() const { return stream.str(); }
};

std::string json1_test = R"({
    "response": {
        "TokInfo": "kb8qjdiIu4bpoc9SexiTpCR0uHzgtF4M",
        "SesInfo": "pNvigDHhDmoR9XL5B0GMiHiLn0Eno1H5i8C9Syiy3RXWICIJkGMvQi0Ye7qaWGaYK84YdOTyVKZD8GlPfl0vQq5q2Hktko4Xoj6D6kfZ7lir6xN6A000xNFy9bw3dfsZ"
    }
}
)";

std::string json2_test = R"({
    "response": "OK"
}
)";

struct F{
        std::thread thr;
        HttpServer server;

        F() {
                server.config.port = 8080;
                server.resource["/api/webserver/SesTokInfo"]["GET"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> ) {
                        try {
                                response->write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><TokInfo>kb8qjdiIu4bpoc9SexiTpCR0uHzgtF4M</TokInfo><SesInfo>pNvigDHhDmoR9XL5B0GMiHiLn0Eno1H5i8C9Syiy3RXWICIJkGMvQi0Ye7qaWGaYK84YdOTyVKZD8GlPfl0vQq5q2Hktko4Xoj6D6kfZ7lir6xN6A000xNFy9bw3dfsZ</SesInfo></response>");
                        }
                        catch(const std::exception &e) {
                                response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
                        }
                };
                server.resource["/api/user/login"]["POST"] = [](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request) {
                        try {
                                if(request->content.string().find("wronguser") == std::string::npos)
                                        response->write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response>OK</response>", {{"Set-Cookie", "admin-cookie"}});
                                else
                                        response->write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><error><message></message><count>1</count><code>108006</code></error>");
                        }
                        catch(const std::exception &e) {
                                response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what());
                        }
                };
                thr=std::thread([this]() {
                                server.start();
                });
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        ~F(){
                server.stop();
                thr.join();
        }
};

BOOST_AUTO_TEST_CASE( auth ) {
        HuaweiAuth ha("admin","passwd");
        BOOST_CHECK_EQUAL(ha.encodeWithToken("token"),"NDg4OTM0YjUyOGU2MTE2ZDczNTE4ZGVkMzM2ZjczNmQxYzYzNTU3N2U0ZDkxZmI2ZDhiMDgxNGQ4ZDA4MjYxMA==");
        BOOST_CHECK_EQUAL(ha.getXmlWithToken("token"),"<?xml version 1.0 encoding=UTF-8?><request><Username>admin</Username><Password>NDg4OTM0YjUyOGU2MTE2ZDczNTE4ZGVkMzM2ZjczNmQxYzYzNTU3N2U0ZDkxZmI2ZDhiMDgxNGQ4ZDA4MjYxMA==</Password><password_type>4</password_type></request>");
}

BOOST_FIXTURE_TEST_CASE( connection , F) {
        HuaweiClient hc("localhost:8080","admin","passwd");
        BOOST_CHECK(!hc.isConnected());
        BOOST_CHECK(!hc.isLoggedIn());

        {
                LogCapture capture;
                hc.login();
                BOOST_CHECK(capture.str().find("passwd") == std::string::npos);
        }
        BOOST_CHECK(hc.isConnected());
        BOOST_CHECK(hc.isLoggedIn());
        BOOST_CHECK_EQUAL(hc.getCookie(),"admin-cookie");
        BOOST_CHECK_EQUAL(hc.getPassword(), security::Credentials::maskSecret("passwd"));
        BOOST_CHECK_NE(hc.getPassword(), "passwd");
        BOOST_CHECK_EQUAL(hc.getPasswordHash(), SimpleWeb::Crypto::to_hex_string(SimpleWeb::Crypto::sha256("passwd")));

        {
                LogCapture capture;
                hc.login("localhost:8080","wronguser","wrongpass");
                BOOST_CHECK(capture.str().find("wrongpass") == std::string::npos);
        }
        BOOST_CHECK(hc.isConnected());
        BOOST_CHECK(!hc.isLoggedIn());
        BOOST_CHECK_EQUAL(hc.getPassword(), security::Credentials::maskSecret("wrongpass"));

        hc.login("localhost:8080","admin","passwd");
        BOOST_CHECK(hc.isConnected());
        BOOST_CHECK(hc.isLoggedIn());
        BOOST_CHECK_EQUAL(hc.getPassword(), security::Credentials::maskSecret("passwd"));

        hc.login("localhost:8081","admin","passwd");
        BOOST_CHECK(!hc.isConnected());
        BOOST_CHECK(!hc.isLoggedIn());

        hc.login("localhost:8080","admin","passwd");
        BOOST_CHECK(hc.isConnected());
        BOOST_CHECK(hc.isLoggedIn());
        BOOST_CHECK_EQUAL(hc.Query("GET","/api/webserver/SesTokInfo"), json1_test);
        BOOST_CHECK_EQUAL(hc.Query("POST","/api/user/login"), json2_test);
}

BOOST_AUTO_TEST_CASE( credentials_helper_masks_and_hashes ) {
        security::Credentials credentials("example","user","secret");
        BOOST_CHECK_EQUAL(credentials.server(), "example");
        BOOST_CHECK_EQUAL(credentials.user(), "user");
        BOOST_CHECK_EQUAL(credentials.password(), "secret");
        BOOST_CHECK_EQUAL(credentials.maskedPassword(), security::Credentials::maskSecret("secret"));
        BOOST_CHECK_EQUAL(credentials.passwordHash(), SimpleWeb::Crypto::to_hex_string(SimpleWeb::Crypto::sha256("secret")));
        BOOST_CHECK(security::Credentials::isMaskedValue(credentials.maskedPassword()));
        credentials.setPassword("");
        BOOST_CHECK(credentials.passwordHash().empty());
        BOOST_CHECK(credentials.maskedPassword().empty());
        BOOST_CHECK(!security::Credentials::isMaskedValue(""));
}
