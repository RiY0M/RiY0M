# Strictest reasonable warning set for GCC, Clang and MSVC.
# Third-party headers (Qt) are included as SYSTEM / external and stay silent.

function(fc_set_warnings target)
    set(gcc_clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wextra-semi
        -Wundef
        -Wmissing-declarations
        -Wzero-as-null-pointer-constant
        -Wsuggest-override
        -Wredundant-decls
    )

    set(gcc_only_warnings
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
        -Warith-conversion
    )

    set(clang_only_warnings
        -Wcomma
        -Wloop-analysis
        -Wunreachable-code-aggressive
        -Wshorten-64-to-32
        -Winconsistent-missing-destructor-override
        -Wmove
        -Wrange-loop-analysis
        -Wshadow-all
        -Wdocumentation
    )

    set(msvc_warnings
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
        /Zc:preprocessor
        /Zc:inline
        /sdl
        /external:anglebrackets
        /external:W0
        /w14242 # conversion, possible loss of data
        /w14254 # larger bit field assigned to smaller bit field
        /w14263 # member function does not override any base class virtual member function
        /w14265 # class has virtual functions, but destructor is not virtual
        /w14287 # unsigned/negative constant mismatch
        /we4289 # loop control variable used outside the for-loop scope
        /w14296 # expression is always true/false
        /w14311 # pointer truncation
        /w14545 /w14546 /w14547 /w14549 /w14555 # suspicious expressions
        /w14619 # unknown pragma warning number
        /w14640 # thread-unsafe static member initialization
        /w14826 # sign-extended conversion
        /w14905 /w14906 # string literal casts
        /w14928 # illegal copy-initialization
    )

    if(MSVC)
        set(warnings ${msvc_warnings})
        if(FC_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(warnings ${gcc_clang_warnings} ${clang_only_warnings})
        if(FC_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(warnings ${gcc_clang_warnings} ${gcc_only_warnings})
        if(FC_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${warnings})

    # Hardening: bounds-checked standard containers in every build type.
    if(NOT MSVC)
        target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)
        if(NOT MINGW) # MinGW would need an extra libssp
            target_compile_options(${target} PRIVATE -fstack-protector-strong)
        endif()
    endif()
endfunction()
