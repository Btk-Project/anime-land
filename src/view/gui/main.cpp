#include "process.hpp"
#include "view/gui/application.hpp"

auto main(int argc, char **argv) -> int {
    anime_land::ProcessGuard process;
    return anime_land::gui::runApplication(argc, argv);
}
