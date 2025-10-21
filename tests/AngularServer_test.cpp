#define BOOST_TEST_MODULE AngularServer_Test
#define BOOST_AUTO_TEST_MAIN
#include <boost/test/unit_test.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/core/null_deleter.hpp>
#include <sstream>
#include "Simple-Web-Server/client_http.hpp"
#include "Simple-Web-Server/server_http.hpp"
#include "AngularServer.hpp"
#include "HuaweiClient.hpp"
#include "src/security/credentials.hpp"

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;
using namespace boost::property_tree;

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

struct mockClient : public RouterClient{
        int logCount = 0;

        bool calLogin = false;
        std::string server;
        std::string user;
        std::string password;
        mockClient(){};
        mockClient(const std::string &srv, const std::string &usr, const std::string &pass) : server(srv), user(usr), password(pass) { };
        void login(const std::string &srv, const std::string &usr, const std::string &pass){ calLogin = true; server = srv; user = usr; password = pass; };
        bool isConnected() const {return true;};
        bool isLoggedIn() const {return true;};
        std::string getUserName(){return user;};
        std::string getPassword(){return password.empty() ? std::string() : security::Credentials::maskSecret(password);};
        std::string getServer(){return server;};
        std::string Query(const std::string &, const std::string &, const std::string &){return "{ \"fakeresponse\":\"ok\"}";};
};

struct F{
		mockClient mc;
		AngularServer as {mc,8080};
		HttpClient client {"localhost:8080"};
		ptree pt;
		~F(){ as.stop(); }
};

struct G{
		int port=8080;
		HuaweiClient hc {"fakehost","fakeuser","fakepasswd"};
		AngularServer as {hc,port};
		HttpClient client {"localhost:8080"};
		ptree pt;
		~G(){ as.stop(); }
};

BOOST_FIXTURE_TEST_CASE( serving_functions , F ){
        auto r1 = client.request("GET", "/config");
        ptree pt;
        std::string body = r1->content.string();
        std::istringstream ss(body);
        read_json(ss, pt);
        BOOST_CHECK_EQUAL(pt.get("config.server",""),"");
        BOOST_CHECK_EQUAL(pt.get("config.user",""),"");
        BOOST_CHECK_EQUAL(pt.get("config.password",""),"");
        BOOST_CHECK(body.find("fakepasswd") == std::string::npos);
        BOOST_CHECK_EQUAL(mc.calLogin, false);
        r1 = client.request("GET", "/api/test");
        body = r1->content.string();
        std::istringstream apiStream(body);
        read_json(apiStream, pt);
        BOOST_CHECK_EQUAL(pt.get("fakeresponse",""),"ok");
//      not required when self hosting
//      auto it = r1->header.find("Access-Control-Allow-Origin");
//      BOOST_CHECK_MESSAGE( it != r1->header.end(), "Header should have Origin set");
}

BOOST_FIXTURE_TEST_CASE( basic_init, G ) {
        auto r1 = client.request("GET", "/config");
        std::string body = r1->content.string();
        std::istringstream ss(body);
        read_json(ss, pt);
        BOOST_CHECK_EQUAL(pt.get("config.server",""),"fakehost");
        BOOST_CHECK_EQUAL(pt.get("config.user",""),"fakeuser");
        BOOST_CHECK_EQUAL(pt.get("config.password",""), security::Credentials::maskSecret("fakepasswd"));
        BOOST_CHECK(body.find("fakepasswd") == std::string::npos);
}

/*
BOOST_FIXTURE_TEST_CASE( afterstart_call, G ) {
        auto oldport = port;
        AngularServer as2(hc,port+1,[](){
                        int x=3;
        });
}
*/

BOOST_FIXTURE_TEST_CASE( serving_data, G ) {
        LogCapture capture;
        auto r = client.request("GET", "/config");
        std::string body = r->content.string();
        std::istringstream configStream(body);
        read_json(configStream, pt);
        BOOST_CHECK_EQUAL(pt.get("config.server",""),"fakehost");
        BOOST_CHECK_EQUAL(pt.get("config.user",""),"fakeuser");
        BOOST_CHECK_EQUAL(pt.get("config.password",""), security::Credentials::maskSecret("fakepasswd"));
        BOOST_CHECK(body.find("fakepasswd") == std::string::npos);

        r = client.request("GET","/ping");
        body = r->content.string();
        std::istringstream pingStream(body);
        read_json(pingStream, pt);
        BOOST_CHECK_EQUAL(pt.get("ping",""),"ok");

        r = client.request("GET","/status");
        body = r->content.string();
        std::istringstream statusStream(body);
        read_json(statusStream, pt);
        BOOST_CHECK_EQUAL(pt.get("status.connected",""),"false");
        BOOST_CHECK_EQUAL(pt.get("status.loggedin",""),"false");

        ptree outpt;
        outpt.put("config.server","new-server");
        outpt.put("config.user","new-user");
        outpt.put("config.password","new-password");

        std::stringstream ss;
        write_json(ss, outpt);
        r = client.request("PUT","/config",ss);
        body = r->content.string();
        std::istringstream putStream(body);
        read_json(putStream, pt);
        BOOST_CHECK_EQUAL(pt.get("config.server",""),"new-server");
        BOOST_CHECK_EQUAL(pt.get("config.user",""),"new-user");
        BOOST_CHECK_EQUAL(pt.get("config.password",""), security::Credentials::maskSecret("new-password"));
        BOOST_CHECK(body.find("new-password") == std::string::npos);
        BOOST_CHECK(capture.str().find("fakepasswd") == std::string::npos);
        BOOST_CHECK(capture.str().find("new-password") == std::string::npos);

        ptree maskedUpdate;
        maskedUpdate.put("config.server","masked-server");
        maskedUpdate.put("config.user","masked-user");
        maskedUpdate.put("config.password", security::Credentials::maskSecret("anything"));
        std::stringstream maskedStream;
        write_json(maskedStream, maskedUpdate);
        r = client.request("PUT","/config", maskedStream);
        BOOST_CHECK_EQUAL(r->status_code, "400");
        body = r->content.string();
        BOOST_CHECK(body.find("Router password must be provided") != std::string::npos);
        BOOST_CHECK(body.find("masked-user") == std::string::npos);
        BOOST_CHECK(body.find("masked-server") == std::string::npos);
        BOOST_CHECK(body.find("new-password") == std::string::npos);
}

