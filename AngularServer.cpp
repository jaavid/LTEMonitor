#include "AngularServer.hpp"
#include "Version.h"
#include "Simple-Web-Server/server_http.hpp"
#include "Simple-Web-Server/status_code.hpp"
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using namespace boost::property_tree;

//------------------------------------- AngularServer

AngularServer::AngularServer(RouterClient &routerClient, const int port,const std::function<void()>& afterStart) : rc(routerClient){
        server.config.port = port;

        const char *resourceOverride = std::getenv("LTE_MONITOR_RESOURCES");
        std::vector<boost::filesystem::path> candidates;
        if(resourceOverride != nullptr) {
                candidates.emplace_back(resourceOverride);
        }
#ifdef ANGULAR_RESOURCE_DIR
        candidates.emplace_back(ANGULAR_RESOURCE_DIR);
#endif
        candidates.emplace_back(boost::filesystem::current_path() / "resources");

        for(const auto &candidate : candidates) {
                if(candidate.empty()) {
                        continue;
                }
                boost::system::error_code ec;
                if(!boost::filesystem::exists(candidate, ec)) {
                        continue;
                }
                auto canonicalPath = boost::filesystem::canonical(candidate, ec);
                if(!ec && boost::filesystem::is_directory(canonicalPath)) {
                        resourceRoot = canonicalPath;
                        break;
                }
        }

        if(resourceRoot.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "Angular resources directory not found";
        } else {
                BOOST_LOG_TRIVIAL(info) << "Angular resources directory: " << resourceRoot.string();
        }

        //out_header.emplace("Access-Control-Allow-Origin","*");
        server.resource["^/ping"]["GET"] = servePingGet;
        server.resource["^/api/(.+)"]["GET"] = std::bind(&AngularServer::serveApiGet, this, std::placeholders::_1, std::placeholders::_2);
        server.resource["^/api/(.+)"]["PUT"] = std::bind(&AngularServer::serveApiGet, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/api/(.+)"]["POST"] = std::bind(&AngularServer::serveApiGet, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/api/(.+)"]["DELETE"] = std::bind(&AngularServer::serveApiGet, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/config"]["GET"] = std::bind(&AngularServer::serveConfigGet, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/config"]["PUT"] = std::bind(&AngularServer::serveConfigPut, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/config"]["OPTIONS"] = std::bind(&AngularServer::serveConfigPut, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/status"]["GET"] = std::bind(&AngularServer::serveStatusGet, this, std::placeholders::_1, std::placeholders::_2);
	server.resource["^/stop"]["GET"] = std::bind(&AngularServer::serveStopGet, this, std::placeholders::_1, std::placeholders::_2);
	server.default_resource["GET"] = std::bind(&AngularServer::serveResources, this, std::placeholders::_1, std::placeholders::_2);

	thr=std::thread([this]() {
			server.start();
			});
	std::this_thread::sleep_for(std::chrono::microseconds(100));

	if(afterStart != NULL){	
		afterStart();
	}
}

void AngularServer::setSingleInHeader(const std::string &key, const std::string &val){
	auto it = out_header.find(key);
	if(it != out_header.end()) it->second = val;	
	else out_header.emplace(key, val);
}	

void AngularServer::stop(){
        server.stop();
        if(std::this_thread::get_id() != thr.get_id() && thr.joinable()){
                thr.join();
        }
}

void AngularServer::serveResources(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> req ){
        BOOST_LOG_TRIVIAL(info) << "sending resources [" << req->path << "] method " << req->method ;
        auto headers = out_header;
        auto filePath = resolveResourcePath(req->path);

        if(filePath.empty()){
                BOOST_LOG_TRIVIAL(warning) << "resource not found: " << req->path;
                headers.emplace("Content-Type", "text/plain; charset=UTF-8");
                res->write(SimpleWeb::StatusCode::client_error_not_found, "Not Found", headers);
                return;
        }

        auto stream = std::make_shared<std::ifstream>(filePath.string(), std::ios::binary);
        if(!*stream){
                BOOST_LOG_TRIVIAL(error) << "failed to open resource: " << filePath.string();
                headers.emplace("Content-Type", "text/plain; charset=UTF-8");
                res->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Unable to read resource", headers);
                return;
        }

        stream->seekg(0, std::ios::end);
        auto length = stream->tellg();
        stream->seekg(0, std::ios::beg);

        auto mimeType = mimeTypeFromPath(filePath);
        if(!mimeType.empty()){
                headers.emplace("Content-Type", mimeType);
        }
        if(length >= 0){
                headers.emplace("Content-Length", std::to_string(length));
        }

        res->write(SimpleWeb::StatusCode::success_ok, *stream, headers);
}

void AngularServer::serveStatusGet(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> ){
	BOOST_LOG_TRIVIAL(info) << "sending status ";
	ptree tree;
	tree.put("status.connected", rc.isConnected());
	tree.put("status.loggedin", rc.isLoggedIn());
	std::stringstream ss;
	write_json(ss,tree);
	setSingleInHeader("Content-Type","application/json; charset=UTF-8");
	res->write(ss,out_header);
}
void AngularServer::serveConfigGet(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> ){
	BOOST_LOG_TRIVIAL(info) << "sending config ";
	ptree tree;
	tree.put("config.server", rc.getServer());
	tree.put("config.user", rc.getUserName());
	tree.put("config.password", rc.getPassword());
	std::ostringstream av, bv;
	av << lte_monitor_VERSION_MAJOR << "." << lte_monitor_VERSION_MINOR;
	bv << BOOST_VERSION / 100000 << "." << BOOST_VERSION / 100 % 1000 << "." << BOOST_VERSION % 100;
	tree.put("config.appVersion", av.str());
	tree.put("config.boostVersion", bv.str());

	std::stringstream ss;
	write_json(ss,tree);
	setSingleInHeader("Content-Type","application/json; charset=UTF-8");
	res->write(ss,out_header);
}

void AngularServer::serveConfigPut(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> req){
	BOOST_LOG_TRIVIAL(info) << "setting new config";
	if(req->method == "OPTIONS"){
		SimpleWeb::CaseInsensitiveMultimap head;
		head.emplace("Access-Control-Allow-Methods","POST, GET, OPTIONS, PUT");
		head.emplace("Access-Control-Allow-Origin","*");
		head.emplace("Access-Control-Allow-Headers","Content-Type, Authorization, X-Requested-With");
		res->write("",head);
		return;
	}
	ptree tree, pt;
	read_json(req->content, pt);
	rc.login(pt.get<std::string>("config.server"),pt.get<std::string>("config.user"),pt.get<std::string>("config.password"));
	tree.put("config.server", rc.getServer());
	tree.put("config.user", rc.getUserName());
	tree.put("config.password", rc.getPassword());
	std::stringstream ss;
	write_json(ss,tree);
	setSingleInHeader("Content-Type","application/json; charset=UTF-8");
	res->write(ss,out_header);
}

void AngularServer::servePingGet(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> ){
	BOOST_LOG_TRIVIAL(info) << "sending ping";
	ptree pt;
	pt.put("ping", "ok");
	std::stringstream ss;
	write_json(ss,pt);
	res->write(ss);
}

void AngularServer::serveApiGet(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> req){
	//std::string mypath=req->path_match[1].str();
	BOOST_LOG_TRIVIAL(info) << "sending api " << req->path ;
	std::string  r = rc.Query(req->method, req->path, req->content.string());
	setSingleInHeader("Content-Type","application/json; charset=UTF-8");
	res->write(r,out_header);
}

void AngularServer::serveStopGet(std::shared_ptr<HttpServer::Response> res, std::shared_ptr<HttpServer::Request> ){
	BOOST_LOG_TRIVIAL(info) << "stopping" ;
	res->write("stopping");
	std::this_thread::sleep_for(std::chrono::microseconds(1000));
	stop();
}

AngularServer::~AngularServer(){
        stop();
}

boost::filesystem::path AngularServer::resolveResourcePath(const std::string &requestPath) const{
        if(resourceRoot.empty()){
                return {};
        }

        std::string relative = requestPath;
        if(!relative.empty() && relative.front() == '/'){
                relative.erase(0,1);
        }
        if(relative.empty()){
                relative = "index.html";
        }

        if(relative.find("..") != std::string::npos){
                BOOST_LOG_TRIVIAL(warning) << "attempt to access parent directory in path: " << requestPath;
                return {};
        }

        auto candidate = resourceRoot / relative;
        boost::system::error_code ec;
        if(boost::filesystem::is_directory(candidate, ec)){
                candidate /= "index.html";
        }

        if(boost::filesystem::exists(candidate, ec) && boost::filesystem::is_regular_file(candidate, ec)){
                return candidate;
        }

        if(relative.find('.') == std::string::npos){
                auto fallback = resourceRoot / "index.html";
                if(boost::filesystem::exists(fallback, ec) && boost::filesystem::is_regular_file(fallback, ec)){
                        return fallback;
                }
        }

        return {};
}

std::string AngularServer::mimeTypeFromPath(const boost::filesystem::path &path){
        auto ext = boost::algorithm::to_lower_copy(path.extension().string());
        if(ext == ".html" || ext == ".htm"){
                return "text/html; charset=UTF-8";
        }
        if(ext == ".css"){
                return "text/css; charset=UTF-8";
        }
        if(ext == ".js"){
                return "application/javascript; charset=UTF-8";
        }
        if(ext == ".json"){
                return "application/json; charset=UTF-8";
        }
        if(ext == ".svg"){
                return "image/svg+xml";
        }
        if(ext == ".png"){
                return "image/png";
        }
        if(ext == ".jpg" || ext == ".jpeg"){
                return "image/jpeg";
        }
        if(ext == ".ico"){
                return "image/x-icon";
        }
        if(ext == ".txt"){
                return "text/plain; charset=UTF-8";
        }
        return "application/octet-stream";
}
