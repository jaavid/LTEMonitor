#ifndef LTEMONITOR_APP_APPLICATION_HPP
#define LTEMONITOR_APP_APPLICATION_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>

#include "ProgramOptions.hpp"
#include "RouterClient.hpp"
#include "AngularServer.hpp"

class Application {
public:
    class ServerController {
    public:
        virtual ~ServerController() = default;
        virtual void stop() = 0;
    };

    using ServerFactory = std::function<std::unique_ptr<ServerController>(
        RouterClient &, int, const std::function<void()> &)>;
    using BrowserLauncher = std::function<void(int)>;

    Application(ProgramOptions &options,
                RouterClient &routerClient,
                ServerFactory serverFactory = {},
                BrowserLauncher browserLauncher = {});

    int run();
    void shutdown();

private:
    static void handleSignal(int signal);
    void restoreSignalHandlers();
    void installSignalHandlers();
    void waitForShutdown();
    void onSignal(int signal);
    bool performLogin();
    void launchBrowserIfNeeded();

    ProgramOptions &options_;
    RouterClient &routerClient_;
    ServerFactory serverFactory_;
    BrowserLauncher browserLauncher_;
    std::unique_ptr<ServerController> server_;
    std::atomic<bool> shutdownRequested_{false};
    std::mutex shutdownMutex_;
    std::condition_variable shutdownCv_;
    void (*previousSigInt_)(int) = nullptr;
    void (*previousSigTerm_)(int) = nullptr;

    static std::atomic<Application *> activeApplication_;
};

#endif // LTEMONITOR_APP_APPLICATION_HPP
