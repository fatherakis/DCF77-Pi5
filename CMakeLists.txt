cmake_minimum_required(VERSION 3.16)

project(
  dcf77_pi5
  VERSION 1.0.0
  DESCRIPTION "DCF77 transmitter for Raspberry Pi 5"
  HOMEPAGE_URL "https://github.com/fatherakis/dcf77-pi5"
  LANGUAGES C
)


# --- Language standard ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Put runtime binaries under build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#--------- Version Handling -------------
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
  @ONLY
)


# --- Options ---
option(DCF77_WERROR "Treat warnings as errors" OFF)
option(DCF77_SANITIZERS "Enable ASan/UBSan (Debug/dev)" OFF)

# --- Common compile defs and warnings ---
add_custom_target(version
  COMMAND ${CMAKE_COMMAND} -E echo "${PROJECT_NAME} v${PROJECT_VERSION}"
)

add_compile_definitions(_GNU_SOURCE)

add_compile_options(-Wall -Wextra)
if(DCF77_WERROR)
  add_compile_options(-Werror)
endif()

# Debug info is useful even in Release sometimes; keep user-controlled via build type.
# (Use -DCMAKE_BUILD_TYPE=Release or Debug)

# --- Dependencies ---
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GPIOD REQUIRED libgpiod)

find_library(PIO_LIBRARY NAMES pio REQUIRED)
find_path(PIO_INCLUDE_DIR NAMES piolib/pio_platform.h piolib/piolib.h REQUIRED)

# --- Include dirs ---
set(PROJ_INC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Hardware library (static) ---
add_library(hw STATIC
src/carrier.c
src/attenuator.c
)

# Force -O3 for Release builds (leave Debug/RelWithDebInfo alone)
target_compile_options(hw PRIVATE $<$<CONFIG:Release>:-O3>)

# --- Main executable ---
add_executable(dcf77-pi5
src/dcf77-pi5.c
src/dcf77.c
src/net_ntp.c
)

# Force -O3 for Release builds (leave Debug/RelWithDebInfo alone)
target_compile_options(dcf77-pi5 PRIVATE $<$<CONFIG:Release>:-O3>)

target_compile_definitions(dcf77-pi5 PRIVATE
PROJECT_NAME="${PROJECT_NAME}"
PROJECT_VERSION="${PROJECT_VERSION}"
PROJECT_AUTHOR="Alexandros Paterakis"
)

target_include_directories(dcf77-pi5 PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${PROJ_INC}
  )

target_include_directories(hw PRIVATE ${PROJ_INC} ${PIO_INCLUDE_DIR} ${PIO_INCLUDE_DIR}/piolib ${GPIOD_INCLUDE_DIRS})
target_link_libraries(hw PUBLIC ${PIO_LIBRARY} ${GPIOD_LIBRARIES} Threads::Threads m)
target_link_libraries(dcf77-pi5 PRIVATE hw)


# --- Sanitizers (optional) ---
if(DCF77_SANITIZERS)
  target_compile_options(hw PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(hw PRIVATE -fsanitize=address,undefined)

  target_compile_options(dcf77-pi5 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(dcf77-pi5 PRIVATE -fsanitize=address,undefined)
endif()

# --- Install rules ---
include(GNUInstallDirs)

# ---- LTO (IPO) in Release ----
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if (ipo_supported)
  set_property(TARGET hw PROPERTY INTERPROCEDURAL_OPTIMIZATION $<$<CONFIG:Release>:TRUE>)
  set_property(TARGET dcf77-pi5 PROPERTY INTERPROCEDURAL_OPTIMIZATION $<$<CONFIG:Release>:TRUE>)
else()
  message(STATUS "IPO/LTO not supported: ${ipo_error}")
endif()

install(TARGETS dcf77-pi5
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PROJECT_NAME}
)