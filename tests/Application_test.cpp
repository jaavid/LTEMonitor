#define BOOST_TEST_MODULE Application_Test
#include <boost/test/unit_test.hpp>

#include "app/Application.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <thread>

class StubRouterClient : public RouterClient {
public:
    void login(const std::string &server, const std::string &user, const std::string &password) override {
        loginCalled = true;
        lastServer = server;
        lastUser = user;
        lastPassword = password;
        if (shouldThrow) {
            throw std::runtime_error("login failure");
        }
        connected = shouldConnect;
        loggedIn = shouldLogin;
    }

    bool isConnected() const override { return connected; }
    bool isLoggedIn() const override { return loggedIn; }
    std::string getUserName() override { return lastUser; }
    std::string getPassword() override { return lastPassword; }
    std::string getServer() override { return lastServer; }
    std::string Query(const std::string &, const std::string &, const std::string &) override { return {}; }

    bool shouldConnect = true;
    bool shouldLogin = true;
    bool shouldThrow = false;
    bool loginCalled = false;
    bool connected = false;
    bool loggedIn = false;
    std::string lastServer;
    std::string lastUser;
    std::string lastPassword;
};

class StubServerController : public Application::ServerController {
public:
    explicit StubServerController(std::function<void()> onStop = {}) : onStop_(std::move(onStop)) {}

    void stop() override {
        ++stopCount;
        stopped = true;
        if (onStop_) {
            onStop_();
        }
    }

    bool stopped = false;
    int stopCount = 0;

private:
    std::function<void()> onStop_;
};

namespace {
void waitUntil(const std::function<bool()> &predicate) {
    for (int i = 0; i < 200 && !predicate(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}

BOOST_AUTO_TEST_CASE(startup_and_shutdown_sequence) {
    char const *argv[] = {"LTEMonitor"};
    ProgramOptions options(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    StubRouterClient router;
    router.shouldConnect = true;
    router.shouldLogin = true;

    bool afterStartCalled = false;
    bool browserCalled = false;
    int launchedPort = 0;
    StubServerController *controller = nullptr;

    Application::ServerFactory factory = [&](RouterClient &, int port, const std::function<void()> &afterStart) {
        auto server = std::unique_ptr<StubServerController>(new StubServerController());
        controller = server.get();
        BOOST_CHECK_EQUAL(port, options.port);
        afterStart();
        afterStartCalled = true;
        return std::unique_ptr<Application::ServerController>(server.release());
    };

    Application app(options, router, factory, [&](int port) {
        browserCalled = true;
        launchedPort = port;
    });

    int result = -1;
    std::thread runner([&]() { result = app.run(); });

    waitUntil([&]() { return afterStartCalled; });
    BOOST_CHECK(afterStartCalled);

    app.shutdown();
    runner.join();

    BOOST_CHECK_EQUAL(result, EXIT_SUCCESS);
    BOOST_CHECK(router.loginCalled);
    BOOST_REQUIRE(controller != nullptr);
    BOOST_CHECK(controller->stopped);
    BOOST_CHECK(browserCalled);
    BOOST_CHECK_EQUAL(launchedPort, options.port);
}

BOOST_AUTO_TEST_CASE(login_failure_prevents_startup) {
    char const *argv[] = {"LTEMonitor"};
    ProgramOptions options(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    StubRouterClient router;
    router.shouldConnect = false;
    router.shouldLogin = false;

    bool factoryCalled = false;

    Application app(options, router,
                    [&](RouterClient &, int, const std::function<void()> &) {
                        factoryCalled = true;
                        return std::unique_ptr<Application::ServerController>(new StubServerController());
                    },
                    [](int) {});

    int result = app.run();

    BOOST_CHECK_EQUAL(result, EXIT_FAILURE);
    BOOST_CHECK(router.loginCalled);
    BOOST_CHECK(!factoryCalled);
}

BOOST_AUTO_TEST_CASE(signal_shutdown_stops_server) {
    char const *argv[] = {"LTEMonitor"};
    ProgramOptions options(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv);
    StubRouterClient router;
    router.shouldConnect = true;
    router.shouldLogin = true;

    std::atomic<bool> afterStartCalled(false);
    StubServerController *controller = nullptr;

    Application app(options, router,
                    [&](RouterClient &, int, const std::function<void()> &afterStart) {
                        auto server = std::unique_ptr<StubServerController>(new StubServerController());
                        controller = server.get();
                        afterStart();
                        afterStartCalled = true;
                        return std::unique_ptr<Application::ServerController>(server.release());
                    },
                    [](int) {});

    int result = -1;
    std::thread runner([&]() { result = app.run(); });

    waitUntil([&]() { return afterStartCalled.load(); });
    BOOST_CHECK(afterStartCalled.load());

    std::raise(SIGTERM);

    runner.join();

    BOOST_CHECK_EQUAL(result, EXIT_SUCCESS);
    BOOST_CHECK(router.loginCalled);
    BOOST_REQUIRE(controller != nullptr);
    BOOST_CHECK(controller->stopped);
}
