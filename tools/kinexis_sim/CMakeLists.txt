cmake_minimum_required(VERSION 3.20)
project(KinexisSimulation LANGUAGES CXX)
enable_testing()
add_library(kinexis_bed bed.cpp gcode.cpp coupon.cpp scene.cpp)
target_compile_features(kinexis_bed PUBLIC cxx_std_17)
target_include_directories(kinexis_bed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(kinexis-sim main.cpp)
target_link_libraries(kinexis-sim PRIVATE kinexis_bed)
add_executable(kinexis-gcode-check gcode_main.cpp)
target_link_libraries(kinexis-gcode-check PRIVATE kinexis_bed)
add_executable(kinexis-coupon coupon_main.cpp)
target_link_libraries(kinexis-coupon PRIVATE kinexis_bed)
add_executable(kinexis-scene-check scene_main.cpp)
target_link_libraries(kinexis-scene-check PRIVATE kinexis_bed)
add_executable(kinexis-sim-tests tests.cpp)
target_link_libraries(kinexis-sim-tests PRIVATE kinexis_bed)
target_compile_definitions(kinexis-sim-tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME bed-kinematics COMMAND kinexis-sim-tests)
add_test(NAME motion-fixture COMMAND kinexis-sim
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/virtual-bed.machine
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/motion.csv)
find_package(Python3 COMPONENTS Interpreter)
set(KINEXIS_SLICER_EXECUTABLE "" CACHE FILEPATH "Optional built PrusaSlicer console for fresh-export integration")
if(Python3_Interpreter_FOUND)
    add_test(NAME cli-validation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_cli.py $<TARGET_FILE:kinexis-sim>)
    add_test(NAME gcode-replay COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_gcode.py $<TARGET_FILE:kinexis-gcode-check>)
    add_test(NAME heightmap-coupon COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_coupon.py $<TARGET_FILE:kinexis-coupon>)
    add_test(NAME collision-scene COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_scene.py $<TARGET_FILE:kinexis-scene-check>)
    if(KINEXIS_SLICER_EXECUTABLE)
        add_test(NAME fresh-planar-export COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_slicer.py
            ${KINEXIS_SLICER_EXECUTABLE} $<TARGET_FILE:kinexis-gcode-check>)
        set_tests_properties(fresh-planar-export PROPERTIES TIMEOUT 180)
    endif()
endif()
