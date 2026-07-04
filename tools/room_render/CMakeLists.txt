# Offline dsp::room IR renderer — the bridge between the C++ implementation
# and the R1-R10 gate functions in notebooks/room_verification.ipynb (render
# here, evaluate there). Built alongside the tests.

add_executable(ambitap_room_render room_render.cpp)
target_link_libraries(ambitap_room_render PRIVATE AmbiTap::ambitap ambitap_warnings)
