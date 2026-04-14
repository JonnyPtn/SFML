# all source files
set(SRC Shader.cpp)

if(SFML_OS_IOS)
    set(RESOURCES
        resources/background.jpg
        resources/billboard.frag
        resources/billboard.geom
        resources/billboard.vert
        resources/blink.frag
        resources/blur.frag
        resources/devices.png
        resources/edge.frag
        resources/sfml_logo.png
        resources/pixelate.frag
        resources/sfml.png
        resources/storm.vert
        resources/text-background.png
        resources/tuffy.ttf
        resources/wave.vert)
    set_source_files_properties(${RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# define the shader target
sfml_add_example(shader GUI_APP
                 SOURCES ${SRC}
                 BUNDLE_RESOURCES ${RESOURCES}
                 DEPENDS SFML::Graphics)
