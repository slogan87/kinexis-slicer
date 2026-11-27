cmake_minimum_required(VERSION 3.20)
project(KinexisSimulation LANGUAGES CXX)
enable_testing()
add_library(kinexis_bed bed.cpp gcode.cpp coupon.cpp material.cpp scene.cpp fit.cpp)
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
add_executable(kinexis-substrate-fit fit_main.cpp)
target_link_libraries(kinexis-substrate-fit PRIVATE kinexis_bed)
add_executable(kinexis-sim-tests tests.cpp)
target_link_libraries(kinexis-sim-tests PRIVATE kinexis_bed)
target_compile_definitions(kinexis-sim-tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME bed-kinematics COMMAND kinexis-sim-tests)
add_executable(kinexis-material-tests material_tests.cpp)
target_link_libraries(kinexis-material-tests PRIVATE kinexis_bed)
target_compile_definitions(kinexis-material-tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME chronological-material COMMAND kinexis-material-tests)
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
    add_test(NAME substrate-surface-fit COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_fit.py $<TARGET_FILE:kinexis-substrate-fit>)
    add_test(NAME planar-curved-transition COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/check_transition.py $<TARGET_FILE:kinexis-coupon>
        $<TARGET_FILE:kinexis-scene-check> $<TARGET_FILE:kinexis-substrate-fit>)
    set_tests_properties(planar-curved-transition PROPERTIES TIMEOUT 120)
    if(KINEXIS_SLICER_EXECUTABLE)
        add_test(NAME fresh-planar-export COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_slicer.py
            ${KINEXIS_SLICER_EXECUTABLE} $<TARGET_FILE:kinexis-gcode-check>)
        set_tests_properties(fresh-planar-export PROPERTIES TIMEOUT 180)
        add_test(NAME fresh-substrate-assessment COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_substrate_slicer.py ${KINEXIS_SLICER_EXECUTABLE}
            $<TARGET_FILE:kinexis-scene-check> $<TARGET_FILE:kinexis-gcode-check> $<TARGET_FILE:kinexis-substrate-fit>)
        set_tests_properties(fresh-substrate-assessment PROPERTIES TIMEOUT 240)
    endif()
endif()
