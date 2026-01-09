# all source files
set(SRC Monitors.cpp)

# define the window target
sfml_add_example(monitors GUI_APP
                 SOURCES ${SRC}
                 DEPENDS SFML::Graphics)
