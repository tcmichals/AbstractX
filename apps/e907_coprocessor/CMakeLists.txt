# apps/e907_coprocessor/CMakeLists.txt
# -----------------------------------
# Deployable ELF/BIN for XuanTie E907 via Linux remoteproc

add_executable(e907_coprocessor
    main.cpp
)

target_link_libraries(e907_coprocessor PRIVATE
    abstractx_target_e907
    abstractx_core
    etl
    m
)

target_link_options(e907_coprocessor PRIVATE
    -T "${E907_LDSCRIPT}"
    -Wl,-Map=e907_coprocessor.map
    -Wl,--gc-sections
    -nostartfiles
)

# Custom command to generate binary (.bin)
add_custom_command(TARGET e907_coprocessor POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:e907_coprocessor> e907_coprocessor.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:e907_coprocessor>
    COMMENT "Generated e907_coprocessor.bin and displaying memory footprint"
)
