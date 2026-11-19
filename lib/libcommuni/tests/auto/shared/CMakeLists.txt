add_library(LibCommuniAutotestShared OBJECT
    tst_ircclientserver.cpp
    tst_ircdata.cpp
)
target_link_libraries(LibCommuniAutotestShared PUBLIC
    Qt${MAJOR_QT_VERSION}::Core
    Qt${MAJOR_QT_VERSION}::Network
    Qt${MAJOR_QT_VERSION}::Test
    LibCommuni::LibCommuni
)
target_include_directories(LibCommuniAutotestShared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
