# AddressSanitizer + UndefinedBehaviorSanitizer (GCC / Clang), AddressSanitizer (MSVC).

function(fc_enable_sanitizers target)
    if(NOT FC_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
    else()
        set(flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${flags})
        target_link_options(${target} PRIVATE ${flags})
    endif()
endfunction()
