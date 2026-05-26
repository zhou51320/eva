#include "runtime/runtime_bootstrap.h"

#include "app/app_bootstrap.h"

void RuntimeBootstrap::applyProcessEnvironment()
{
    AppBootstrap::applyEarlyEnv();
    AppBootstrap::applyLinuxRuntimeEnv();
}

AppContext RuntimeBootstrap::prepareContext()
{
    return prepareContext(Options());
}

AppContext RuntimeBootstrap::prepareContext(const Options &options)
{
    AppContext context = AppBootstrap::buildContext();
    if (options.ensureTempDir)
    {
        AppBootstrap::ensureTempDir(context);
    }
    if (options.ensureDefaultConfig)
    {
        AppBootstrap::ensureDefaultConfig(context);
    }
    return context;
}
