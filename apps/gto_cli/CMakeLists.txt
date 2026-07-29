add_executable(gto_cli main.cpp)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(gto_cli
  PRIVATE
    gtosd::core
    gtosd::tree
    gtosd::isomorphism
    gtosd::solver
    gtosd::best_response
    gtosd::memory
    gtosd::postflop
    gtosd::storage
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gto_cli)
