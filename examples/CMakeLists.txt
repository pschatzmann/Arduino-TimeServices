# Only the desktop simulator can be built here: esp32-s3-hosyond needs the
# real board (WiFi, Bluetooth, display).
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

arduino_sketch(desktop-simulator desktop-simulator/desktop-simulator.ino
               LIBRARIES Arduino-TimeServices SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS} m)
target_compile_features(desktop-simulator PRIVATE cxx_std_17)
target_compile_definitions(desktop-simulator PRIVATE TINYGPU_AUTO_NAMESPACE TINYMD_AUTO_NAMESPACE)
