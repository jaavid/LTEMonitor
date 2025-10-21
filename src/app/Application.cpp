#include "Application.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
class AngularServerController : public Application::ServerController {
public:
    AngularServerController(RouterClient &client, int port, const std::function<void()> &afterStart)
        : server_(client, port, afterStart) {}

    void stop() override {
        server_.stop();
    }

private:
    AngularServer server_;
};
}

std::atomic<Application *> Application::activeApplication_{nullptr};

Application::Application(ProgramOptions &options,
                         RouterClient &routerClient,
                         ServerFactory serverFactory,
                         BrowserLauncher browserLauncher)
    : options_(options),
      routerClient_(routerClient),
      serverFactory_(std::move(serverFactory)),
      browserLauncher_(std::move(browserLauncher)) {
    if (!serverFactory_) {
        serverFactory_ = [](RouterClient &client, int port, const std::function<void()> &afterStart) {
            return std::unique_ptr<ServerController>(new AngularServerController(client, port, afterStart));
        };
    }

    if (!browserLauncher_) {
        browserLauncher_ = [](int port) {
            std::string command = "xdg-open http://localhost:";
    #ifdef _WIN32
            command = "cmd /c start http://localhost:";
    #elif __APPLE__
            command = "open http://localhost:";
    #endif
            std::system((command + std::to_string(port)).c_str());
        };
    }
}

int Application::run() {
    if (!options_.proceed) {
        std::cout << "exiting ... " << std::endl;
        return EXIT_SUCCESS;
    }

    shutdownRequested_.store(false);
    installSignalHandlers();

    bool loginSucceeded = performLogin();
    if (!loginSucceeded) {
        restoreSignalHandlers();
        return EXIT_FAILURE;
    }

    if (shutdownRequested_.load()) {
        restoreSignalHandlers();
        return EXIT_SUCCESS;
    }

    auto afterStart = [this]() { launchBrowserIfNeeded(); };
    server_ = serverFactory_(routerClient_, options_.port, afterStart);
    if (shutdownRequested_.load()) {
        server_->stop();
    }

    waitForShutdown();

    server_.reset();
    restoreSignalHandlers();
    return EXIT_SUCCESS;
}

void Application::shutdown() {
    bool expected = false;
    if (!shutdownRequested_.compare_exchange_strong(expected, true)) {
        return;
    }

    if (server_) {
        server_->stop();
    }

    shutdownCv_.notify_all();
}

bool Application::performLogin() {
    auto future = std::async(std::launch::async, [this]() {
        routerClient_.login(options_.server, options_.user, options_.password);
        return routerClient_.isConnected() && routerClient_.isLoggedIn();
    });

    try {
        if (!future.get()) {
            std::cerr << "Failed to login to router '" << options_.server << "'" << std::endl;
            return false;
        }
    } catch (const std::exception &ex) {
        std::cerr << "Login exception: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Login failed with unknown error" << std::endl;
        return false;
    }

    return true;
}

void Application::launchBrowserIfNeeded() {
    if (options_.preventGui || !browserLauncher_) {
        return;
    }
    browserLauncher_(options_.port);
}

void Application::waitForShutdown() {
    std::unique_lock<std::mutex> lock(shutdownMutex_);
    if (!shutdownRequested_.load()) {
        shutdownCv_.wait(lock, [this]() { return shutdownRequested_.load(); });
    }
}

void Application::handleSignal(int signal) {
    auto *app = activeApplication_.load();
    if (app) {
        app->onSignal(signal);
    }
}

void Application::onSignal(int) {
    shutdown();
}

void Application::installSignalHandlers() {
    activeApplication_.store(this);
    previousSigInt_ = std::signal(SIGINT, &Application::handleSignal);
    previousSigTerm_ = std::signal(SIGTERM, &Application::handleSignal);
}

void Application::restoreSignalHandlers() {
    if (previousSigInt_ && previousSigInt_ != SIG_ERR) {
        std::signal(SIGINT, previousSigInt_);
    } else {
        std::signal(SIGINT, SIG_DFL);
    }
    if (previousSigTerm_ && previousSigTerm_ != SIG_ERR) {
        std::signal(SIGTERM, previousSigTerm_);
    } else {
        std::signal(SIGTERM, SIG_DFL);
    }
    previousSigInt_ = nullptr;
    previousSigTerm_ = nullptr;
    activeApplication_.store(nullptr);
}
