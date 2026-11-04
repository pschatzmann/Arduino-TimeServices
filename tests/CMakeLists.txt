# Each test_*.cpp is its own executable and CTest case.
file(GLOB TS_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

foreach(test_source ${TS_TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE Arduino-TimeServices arduino_emulator)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
