# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET esp32s3)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(atividade_tflite_dht22)

# O Wokwi pode apresentar problemas com alguns kernels otimizados do ESP-NN.
# Mantemos o componente original e removemos essa macro somente no build.
idf_component_get_property(tflite_micro_lib espressif__esp-tflite-micro COMPONENT_LIB)
if(tflite_micro_lib)
    target_compile_options(${tflite_micro_lib} PRIVATE -UESP_NN)
endif()
