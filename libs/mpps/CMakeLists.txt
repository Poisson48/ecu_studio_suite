find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)

if(NOT ECU_MPPS_SIMULATION)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    set(MPPS_LIBUSB_SRC src/MppsDevice_libusb.cpp)
else()
    set(LIBUSB_INCLUDE_DIRS "")
    set(LIBUSB_LIBRARIES "")
    set(MPPS_LIBUSB_SRC "")
endif()

add_library(mppslib STATIC
    src/MppsDevice.cpp
    src/MppsDevice_sim.cpp
    ${MPPS_LIBUSB_SRC}
)

target_include_directories(mppslib
    PUBLIC  include
    PRIVATE src
            ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(mppslib
    PUBLIC  Qt6::Core
    PRIVATE ${LIBUSB_LIBRARIES}
)

target_compile_options(mppslib PRIVATE -Wall -Wextra -Wpedantic)
