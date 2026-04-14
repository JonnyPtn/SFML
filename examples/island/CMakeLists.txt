find_package(Threads REQUIRED)

# all source files
set(SRC Island.cpp)

if(SFML_OS_IOS)
    set(RESOURCES
        resources/terrain.frag
        resources/terrain.vert
        resources/tuffy.ttf)
    set_source_files_properties(${RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# define the island target
sfml_add_example(island GUI_APP
                 SOURCES ${SRC}
                 BUNDLE_RESOURCES ${RESOURCES}
                 DEPENDS SFML::Graphics)

# external dependency headers
target_include_directories(island SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/examples/island)

target_link_libraries(island PRIVATE Threads::Threads)
