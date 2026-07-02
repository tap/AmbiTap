add_library(ambitap_capi SHARED ambitap_capi.cpp)
target_link_libraries(ambitap_capi PRIVATE AmbiTap::ambitap ambitap_warnings)
set_target_properties(ambitap_capi PROPERTIES
    OUTPUT_NAME ambitap_capi
    CXX_VISIBILITY_PRESET hidden)
