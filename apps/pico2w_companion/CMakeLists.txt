# apps/pico2w_companion/CMakeLists.txt

if(TARGET abstractx_target_pico2w)
    add_executable(pico2w_companion
        main.cpp
    )

    target_link_libraries(pico2w_companion PRIVATE
        abstractx_target_pico2w
        abstractx_core
        etl
    )

    pico_add_extra_outputs(pico2w_companion)
endif()
