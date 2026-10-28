cmake_minimum_required(VERSION 3.24)
project(PaleoGettingStarted LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++ 的“导包”包含两件事：找到头文件和库，再把库链接到自己的程序。
# 配置时在 CMAKE_PREFIX_PATH 中填写本项目 sdk 和 Qt 的安装目录。
find_package(PaleoDataService CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

foreach(example IN ITEMS
    01_list_wells
    02_curve_by_name
    03_curve_statistics
    04_read_intervals
    05_read_time_depth
    06_feature_table
    07_export_curve_json
    08_edit_temporary_project
    09_segyio
    10_hd_seismic
    11_time_slice_region)
    add_executable(example_${example} ${example}/main.cpp)
    target_link_libraries(example_${example} PRIVATE
        Paleo::paleo_data_service Qt6::Core)
    if(MSVC)
        target_compile_options(example_${example} PRIVATE /utf-8)
    endif()
endforeach()
