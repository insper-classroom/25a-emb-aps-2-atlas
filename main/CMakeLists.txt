add_executable(pico_emb
        hc06.c
        main.c
)

set_target_properties(pico_emb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_libraries(pico_emb pico_stdlib freertos hardware_adc hardware_i2c hardware_uart Fusion)
pico_add_extra_outputs(pico_emb)
