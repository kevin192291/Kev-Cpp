#include "app/Application.h"

int main(int argc, char* argv[]) {
    auto app = kevc::Application::create();
    return app->run(argc, argv);
}
