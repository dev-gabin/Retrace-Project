cmake_minimum_required(VERSION 3.16)
project(server_jetson VERSION 0.1 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET libmariadb)
find_package(JPEG REQUIRED)
find_package(Threads REQUIRED)

add_executable(server_jetson
  server_main.c
  server/server.c
  server/db_handler.c
  server/device_handler.c
  database/db_store.c
  database/jpeg_check.c
)
target_include_directories(server_jetson PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/server
  ${CMAKE_CURRENT_SOURCE_DIR}/database
)
target_link_libraries(server_jetson PRIVATE
  PkgConfig::MARIADB JPEG::JPEG Threads::Threads
)
target_compile_options(server_jetson PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(retrace_db_store STATIC database/db_store.c database/jpeg_check.c)
target_include_directories(retrace_db_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/database)
target_link_libraries(retrace_db_store PRIVATE PkgConfig::MARIADB JPEG::JPEG)
target_compile_options(retrace_db_store PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(retrace-db database/db_store_cli.c)
target_link_libraries(retrace-db PRIVATE retrace_db_store)
target_compile_options(retrace-db PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS server_jetson retrace_db_store retrace-db
  ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
install(FILES database/db_store.h DESTINATION include/retrace)
