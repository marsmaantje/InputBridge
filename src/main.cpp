#include <SDL3/SDL_main.h>

#include "App/Application.h"

int main(int /*argc*/, char* /*argv*/[])
{
    Application app;

    if (!app.Init())
        return 1;

    app.Run();
    app.Shutdown();

    return 0;
}
