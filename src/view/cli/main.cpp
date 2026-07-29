#include "process.hpp"
#include "view/cli/application.hpp"

auto main(int argc, char **argv) -> int {
    anime_land::ProcessGuard process;
    return anime_land::cli::runApplication(argc, argv);
}
