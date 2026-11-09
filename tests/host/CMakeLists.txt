cmake_minimum_required(VERSION 3.16)

project(battlestation_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror)

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../main/engine")

add_executable(test_fft
    test_fft.c
    "${ENGINE_DIR}/audio_fft.c"
    "${ENGINE_DIR}/audio_features.c"
)
target_include_directories(test_fft PRIVATE "${ENGINE_DIR}")
target_link_libraries(test_fft PRIVATE m)

add_executable(test_features
    test_features.c
    "${ENGINE_DIR}/audio_fft.c"
    "${ENGINE_DIR}/audio_features.c"
)
target_include_directories(test_features PRIVATE "${ENGINE_DIR}")
target_link_libraries(test_features PRIVATE m)

add_executable(test_mixer
    test_mixer.c
    "${ENGINE_DIR}/audio_mixer.c"
)
target_include_directories(test_mixer PRIVATE "${ENGINE_DIR}")
target_link_libraries(test_mixer PRIVATE m)

add_executable(test_organism
    test_organism.c
    "${ENGINE_DIR}/organism_engine.c"
)
target_include_directories(test_organism PRIVATE "${ENGINE_DIR}")
target_link_libraries(test_organism PRIVATE m)

enable_testing()

add_test(NAME fft_spectrum COMMAND test_fft)
add_test(NAME features_levels COMMAND test_features)
add_test(NAME mixer COMMAND test_mixer)
add_test(NAME organism COMMAND test_organism)
