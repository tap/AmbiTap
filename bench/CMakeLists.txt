add_executable(ambitap_bench bench_main.cpp)
target_link_libraries(ambitap_bench PRIVATE AmbiTap::ambitap ambitap_warnings)
