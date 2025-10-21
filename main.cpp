#include "ProgramOptions.hpp"
#include "HuaweiClient.hpp"
#include "app/Application.hpp"

int main(int argc, const char *argv[]) {
        ProgramOptions options(argc, argv);
        HuaweiClient router(options.server, options.user, options.password);
        Application application(options, router);
        return application.run();
}
