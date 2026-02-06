cmake_minimum_required(VERSION 3.14)
project(xyz2las)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Boost is a dependency of libLAS
find_package(Boost REQUIRED)

# Fix for broken system ZLIB Config (e.g. missing static libs)
# Redirect ZLIB Config search to a place where it won't find it, forcing fallback to Module
list(APPEND CMAKE_SYSTEM_IGNORE_PATH "/usr/lib64/cmake/ZLIB")
list(APPEND CMAKE_SYSTEM_IGNORE_PATH "/lib64/cmake/ZLIB")
find_package(ZLIB MODULE REQUIRED)

# 1. Try to find libLAS locally
find_package(libLAS QUIET)

if(libLAS_FOUND)
    message(STATUS "Found libLAS locally at: ${libLAS_LIBRARIES}")
    set(XYZ2LAS_LIBS ${libLAS_LIBRARIES})
    include_directories(${libLAS_INCLUDE_DIR})
else()
    message(STATUS "libLAS not found locally. Fetching via FetchContent...")
    include(FetchContent)

    # Force static build for internal dependencies to avoid runtime linkage issues
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
    set(BUILD_STATIC ON CACHE BOOL "Build static libraries (LASzip)" FORCE)

    # Pre-set options to enable dependencies before libLAS project is loaded
    # Using both normal variables and CACHE FORCE to ensure they take precedence
    set(WITH_GDAL ON)
    set(WITH_GEOTIFF ON)
    set(WITH_LASZIP ON)
    set(WITH_TESTS OFF)
    set(WITH_UTILITIES OFF)

    set(WITH_GDAL ON CACHE BOOL "Choose if GDAL support should be built" FORCE)
    set(WITH_GEOTIFF ON CACHE BOOL "Choose if GeoTIFF support should be built" FORCE)
    set(WITH_LASZIP ON CACHE BOOL "Choose if LASzip support should be built" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "Choose if tests should be built" FORCE)
    set(WITH_UTILITIES OFF CACHE BOOL "Choose if utilities should be built" FORCE)

    # -------------------------------------------------------------------------
    # Handle Dependencies for libLAS (LASzip, GeoTIFF, GDAL)
    # -------------------------------------------------------------------------
    
    # 1. LASzip
    find_package(LASzip QUIET)
    if(LASzip_FOUND)
         message(STATUS "Found LASzip locally: ${LASzip_DIR}")
    else()
         message(STATUS "LASzip not found locally. Fetching...")
         FetchContent_Declare(
             laszip
             GIT_REPOSITORY https://github.com/LASzip/LASzip.git
             GIT_TAG        v2.2.0
         )
         
         FetchContent_GetProperties(laszip)
         if(NOT laszip_POPULATED)
             FetchContent_Populate(laszip)
             
             # Patch LASzip to rename 'dist' target to avoid collision with libLAS's 'dist'
             file(READ "${laszip_SOURCE_DIR}/CMakeLists.txt" laszip_cmake_original)
             string(REPLACE "add_custom_target(dist" "add_custom_target(laszip_dist" laszip_cmake_patched "${laszip_cmake_original}")
             
             # Patch cmake_minimum_required to fix "Compatibility with CMake < 3.5 has been removed"
             string(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9]+\\.[0-9]+(\\.[0-9]+)?\\)" "cmake_minimum_required(VERSION 3.10)" laszip_cmake_patched "${laszip_cmake_patched}")
             
             file(WRITE "${laszip_SOURCE_DIR}/CMakeLists.txt" "${laszip_cmake_patched}")
             
             add_subdirectory(${laszip_SOURCE_DIR} ${laszip_BINARY_DIR})
         endif()

         # libLAS looks for LASzip using find_package behavior or strict include/libs.
         # For LASzip 2.x, headers might be in include/laszip/
         # libLAS FindLASzip expects to find laszip.hpp at LASZIP_INCLUDE_DIR/laszip.hpp
         set(LASZIP_INCLUDE_DIR ${laszip_SOURCE_DIR}/include/laszip CACHE PATH "" FORCE)
         set(LASZIP_LIBRARY laszip CACHE FILEPATH "" FORCE)
    endif()

    # 2. GeoTIFF
    find_package(GeoTIFF QUIET)
    if(GeoTIFF_FOUND)
        message(STATUS "Found GeoTIFF locally.")
    else()
        message(STATUS "GeoTIFF not found. Fetching via FetchContent...")
        
        # GeoTIFF requires TIFF and PROJ
        find_package(TIFF REQUIRED)
        find_package(PROJ QUIET) 
        # Modern PROJ (6+) uses PROJ CONFIG, older uses FindPROJ. 
        # If not found, libgeotiff might fail.
        
        FetchContent_Declare(
            libgeotiff
            GIT_REPOSITORY https://github.com/OSGeo/libgeotiff.git
            GIT_TAG        1.7.1
        )
        FetchContent_GetProperties(libgeotiff)
        if(NOT libgeotiff_POPULATED)
            FetchContent_Populate(libgeotiff)
            
            # Patch libgeotiff to update cmake_minimum_required
            set(geotiff_cmakelists "${libgeotiff_SOURCE_DIR}/libgeotiff/CMakeLists.txt")
            file(READ "${geotiff_cmakelists}" geotiff_cmake_content)
            string(REGEX REPLACE "[cC][mM][aA][kK][eE]_[mM][iI][nN][iI][mM][uU][mM]_[rR][eE][qQ][uU][iI][rR][eE][dD]\\(VERSION [0-9]+\\.[0-9]+(\\.[0-9]+)?\\)" "cmake_minimum_required(VERSION 3.10)" geotiff_cmake_patched "${geotiff_cmake_content}")
            file(WRITE "${geotiff_cmakelists}" "${geotiff_cmake_patched}")

            # libgeotiff CMakeLists.txt is in 'libgeotiff' subdirectory
            add_subdirectory(${libgeotiff_SOURCE_DIR}/libgeotiff ${libgeotiff_BINARY_DIR})
        endif()
        
        # Help libLAS find the fetched GeoTIFF
        # In libgeotiff 1.7.0+, headers are in 'libgeotiff' subdir of source
        set(GEOTIFF_INCLUDE_DIR ${libgeotiff_SOURCE_DIR}/libgeotiff CACHE PATH "" FORCE)
        
        # Include paths for compilation of libLAS (needs both source headers and generated config)
        # generated config is in the binary directory we passed to add_subdirectory
        # libxtiff subfolder is also needed for xtiffio.h
        include_directories(SYSTEM 
            ${libgeotiff_SOURCE_DIR}/libgeotiff 
            ${libgeotiff_BINARY_DIR}
            ${libgeotiff_SOURCE_DIR}/libgeotiff/libxtiff
        )

        # Target name in libgeotiff CMakeLists is usually 'geotiff_library' (static/shared)
        set(GEOTIFF_LIBRARY geotiff_library CACHE FILEPATH "" FORCE)
        set(GEOTIFF_LIBRARIES geotiff_library CACHE FILEPATH "" FORCE)
        
        # Ensure we link against it
        list(APPEND XYZ2LAS_LIBS geotiff_library)
    endif()
    
    # Use 'master' branch for better compatibility with modern Boost versions
    FetchContent_Declare(
        liblas
        GIT_REPOSITORY https://github.com/libLAS/libLAS.git
        GIT_TAG        master 
    )

    FetchContent_GetProperties(liblas)
    if(NOT liblas_POPULATED)
        FetchContent_Populate(liblas)
        
        # Patch libLAS main CMakeLists.txt for cmake_minimum_required compatibility
        set(liblas_root_cmake "${liblas_SOURCE_DIR}/CMakeLists.txt")
        if(EXISTS "${liblas_root_cmake}")
            file(READ "${liblas_root_cmake}" liblas_root_content)
            string(REGEX REPLACE "[cC][mM][aA][kK][eE]_[mM][iI][nN][iI][mM][uU][mM]_[rR][eE][qQ][uU][iI][rR][eE][dD]\\(VERSION [0-9]+\\.[0-9]+(\\.[0-9]+)?\\)" "cmake_minimum_required(VERSION 3.10)" liblas_root_patched "${liblas_root_content}")
            file(WRITE "${liblas_root_cmake}" "${liblas_root_patched}")
        endif()
        
        # Patch libLAS to fix export error (dependent target 'laszip' not in export set)
        
        # Patch src/CMakeLists.txt (Target installation)
        set(liblas_src_cmake "${liblas_SOURCE_DIR}/src/CMakeLists.txt")
        if(EXISTS "${liblas_src_cmake}")
            file(READ "${liblas_src_cmake}" liblas_cmake_content)
            string(REPLACE "EXPORT depends" "" liblas_cmake_content "${liblas_cmake_content}")
            # Enforce static build by removing explicit SHARED keyword
            string(REPLACE "add_library(las SHARED" "add_library(las" liblas_cmake_content "${liblas_cmake_content}")
            string(REPLACE "add_library(las_c SHARED" "add_library(las_c" liblas_cmake_content "${liblas_cmake_content}")
            file(WRITE "${liblas_src_cmake}" "${liblas_cmake_content}")
        endif()
        
        # Patch cmake/CMakeLists.txt (Export file generation)
        set(liblas_cmake_cmake "${liblas_SOURCE_DIR}/cmake/CMakeLists.txt")
        if(EXISTS "${liblas_cmake_cmake}")
            file(READ "${liblas_cmake_cmake}" liblas_cmake_content)
            # Replace install(EXPORT ...) with a message() command to consume the arguments without error
            string(REPLACE "install (EXPORT depends" "message(STATUS \"Skipping install export depends\"" liblas_cmake_content "${liblas_cmake_content}")
            file(WRITE "${liblas_cmake_cmake}" "${liblas_cmake_content}")
        endif()
        
        # Patch cmake/modules/FindGDAL.cmake to fix library finding logic
        set(liblas_findgdal_cmake "${liblas_SOURCE_DIR}/cmake/modules/FindGDAL.cmake")
        if(EXISTS "${liblas_findgdal_cmake}")
            file(READ "${liblas_findgdal_cmake}" liblas_findgdal_content)
            # Replace the broken manual library path construction for Linux with find_library
            string(REPLACE 
                "SET(GDAL_LIBRARY \${GDAL_LINK_DIRECTORIES}/lib\${GDAL_LIB_NAME}.so CACHE STRING INTERNAL)"
                "find_library(GDAL_LIBRARY NAMES gdal \${GDAL_LIB_NAME} HINTS \${GDAL_LINK_DIRECTORIES})"
                liblas_findgdal_content "${liblas_findgdal_content}")
            file(WRITE "${liblas_findgdal_cmake}" "${liblas_findgdal_content}")
        endif()
        
        # Patch root CMakeLists.txt (dist target collision) -> already fixed in laszip, but libLAS also has it.
        # libLAS 'dist' target works if LASzip 'dist' renamed.
        
        add_subdirectory(${liblas_SOURCE_DIR} ${liblas_BINARY_DIR})
    endif()

    set(XYZ2LAS_LIBS las)
    set(XYZ2LAS_INCLUDES ${liblas_SOURCE_DIR}/include)
endif()

add_executable(xyz2las main.cpp)
target_link_libraries(xyz2las PRIVATE ${XYZ2LAS_LIBS})
if(XYZ2LAS_INCLUDES)
    target_include_directories(xyz2las PRIVATE ${XYZ2LAS_INCLUDES})
endif()

# Ensure Boost headers are available
target_include_directories(xyz2las PRIVATE ${Boost_INCLUDE_DIRS})
