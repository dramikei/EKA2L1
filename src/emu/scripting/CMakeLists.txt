add_library(symemu
    include/scripting/cpu.h
    include/scripting/emulog.h
    include/scripting/instance.h
    include/scripting/process.h
    include/scripting/thread.h
    include/scripting/mem.h
    include/scripting/hook.h
    include/scripting/symemu.inl
    src/cpu.cpp
    src/emulog.cpp
    src/process.cpp
    src/mem.cpp
    src/hook.cpp
    src/thread.cpp
    src/instance.cpp)

pybind11_add_module(symemu_common
    include/scripting/common.h
    src/common.cpp)

target_include_directories(symemu PUBLIC include  ${EPOC32_INCLUDE_DIR})
target_link_libraries(symemu PRIVATE common epoc epockern manager)

if (ENABLE_SCRIPTING_ABILITY)
    target_link_libraries(symemu PUBLIC pybind11)
endif (ENABLE_SCRIPTING_ABILITY)

if (UNIX OR APPLE)
    target_link_libraries(symemu PRIVATE util)
endif()

target_include_directories(symemu_common PUBLIC include)
target_link_libraries(symemu_common PRIVATE common pybind11)

if (UNIX OR APPLE)
    target_link_libraries(symemu_common PRIVATE util)
endif()

# Move the syemu to script folder
set_target_properties(symemu PROPERTIES OUTPUT_NAME symemu
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

set_target_properties(symemu_common PROPERTIES OUTPUT_NAME common
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/scripts/symemu2"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/scripts/symemu2"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/scripts/symemu2")

add_custom_command(
	TARGET symemu
			POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:symemu>/scripts/example"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/README.md" "$<TARGET_FILE_DIR:symemu>/scripts/example/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/example/hello_eka2l1.py" "$<TARGET_FILE_DIR:symemu>/scripts/example/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/example/hello_panic.py" "$<TARGET_FILE_DIR:symemu>/scripts/example/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/example/hello_svc.py" "$<TARGET_FILE_DIR:symemu>/scripts/example/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/example/hello_reschedule.py" "$<TARGET_FILE_DIR:symemu>/scripts/example/")

add_custom_command(
	TARGET symemu_common
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/symemu2/__init__.py" "$<TARGET_FILE_DIR:symemu>/scripts/symemu2/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/symemu2/events.py" "$<TARGET_FILE_DIR:symemu>/scripts/symemu2/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/symemu2/svc.py" "$<TARGET_FILE_DIR:symemu>/scripts/symemu2/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/emu/scripting/python/symemu2/descriptor.py" "$<TARGET_FILE_DIR:symemu>/scripts/symemu2/")