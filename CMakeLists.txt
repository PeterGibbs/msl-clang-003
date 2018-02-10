#CMakeLists.txt for cmocka on WINDOWS only,
#Download cmocka-1.1.0-mingw,zip and extract files
#be sure that CMOCKA_MAIN_DIR is set to the folder you extracted to
cmake_minimum_required(VERSION 3.3)
project(msl-clang-003)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Werror")

set(SOURCE_FILES
    main.c mem_pool.c test_suite.h test_suite.c)

set(CMOCKA_MAIN_DIR G:/cmocka-1.1.0)




add_executable(msl-clang-003 ${SOURCE_FILES})

include_directories(${CMOCKA_MAIN_DIR}/include)
link_directories(${CMOCKA_MAIN_DIR}/bin)
link_directories(${CMOCKA_MAIN_DIR}/lib)

target_link_libraries(msl-clang-003 ${CMOCKA_MAIN_DIR}/lib/libcmocka.dll.a)
#The program will only run if the dll it needs is in the same folder
add_custom_target(copyDll
        COMMAND ${CMAKE_COMMAND} -E copy ${CMOCKA_MAIN_DIR}/bin/cmocka.dll ${CMAKE_BINARY_DIR}
        COMMENT "Copying dll"
        )

add_dependencies(msl-clang-003 copyDll)