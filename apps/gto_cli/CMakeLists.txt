add_executable(gto_cli main.cpp)
target_link_libraries(gto_cli
  PRIVATE
    gtosd::core
    gtosd::tree
    gtosd::isomorphism
    gtosd::solver
    gtosd::best_response)
gtosd_set_warnings(gto_cli)
