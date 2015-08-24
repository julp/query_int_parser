cmake_minimum_required(VERSION 2.8.8)

project(query_int_parser C)

option(DEBUG "Enable/disable debugging" ON)
option(POSTGRESQL "Build for use inside PostgreSQL instead of standalone" OFF)

set(DEFINITIONS )
set(SOURCES parser.c stack.c hashtable.c parsenum.c)


function(debug _VARNAME)
    if(DEBUG)
        if(DEFINED ${_VARNAME})
            message("${_VARNAME} = ${${_VARNAME}}")
        else(DEFINED ${_VARNAME})
            message("${_VARNAME} = <UNDEFINED>")
        endif(DEFINED ${_VARNAME})
    endif(DEBUG)
endfunction(debug)

if(POSTGRESQL)

    find_program(PG_CONFIG_EXECUTABLE "pg_config")
    if(NOT PG_CONFIG_EXECUTABLE)
        message(FATAL_ERROR "enable to find pg_config")
    endif(NOT PG_CONFIG_EXECUTABLE)

    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --includedir OUTPUT_VARIABLE PG_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --includedir-server OUTPUT_VARIABLE PG_SERVER_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --cflags OUTPUT_VARIABLE PG_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --cppflags OUTPUT_VARIABLE PG_CPPFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --cflags_sl OUTPUT_VARIABLE PG_CFLAGS_SL OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --ldflags OUTPUT_VARIABLE PG_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --ldflags_sl OUTPUT_VARIABLE PG_LDFLAGS_SL OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --pkglibdir OUTPUT_VARIABLE PG_PKG_LIBRARY_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
#     execute_process(COMMAND ${PG_CONFIG_EXECUTABLE} --X OUTPUT_VARIABLE PG_X OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(DEBUG TRUE)
    debug("PG_INCLUDE_DIR")
    debug("PG_SERVER_INCLUDE_DIR")
    debug("PG_CPPFLAGS")
    debug("PG_CFLAGS")
    debug("PG_CFLAGS_SL")
    debug("PG_LDFLAGS")
    debug("PG_LDFLAGS_SL")
    debug("PG_PKG_LIBRARY_DIR")

    include_directories(${PG_SERVER_INCLUDE_DIR})
    add_library(${CMAKE_PROJECT_NAME} SHARED ${SOURCES})

else(POSTGRESQL)

    add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})

endif(POSTGRESQL)

if(DEBUG)
    list(APPEND DEFINITIONS "DEBUG=1")
endif(DEBUG)

if(POSTGRESQL)
    list(APPEND DEFINITIONS "POSTGRESQL=1")
endif(POSTGRESQL)

if(DEFINITIONS)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        PREFIX ""
        COMPILE_FLAGS "${PG_CPPFLAGS} ${PG_CFLAGS} ${PG_CFLAGS_SL}"
        LINK_FLAGS "${PG_LDFLAGS} ${PG_LDFLAGS_SL}"
        COMPILE_DEFINITIONS "${DEFINITIONS}"
    )

    if(POSTGRESQL)
        install(
            TARGETS ${CMAKE_PROJECT_NAME}
            LIBRARY DESTINATION ${PG_PKG_LIBRARY_DIR}
        )
        get_target_property(BUILD_LOCATION ${CMAKE_PROJECT_NAME} LOCATION)
        get_filename_component(BUILD_NAME ${BUILD_LOCATION} NAME)
        install(
            CODE "message(\"
CREATE FUNCTION compile_query_int(text, bool, bool)
RETURNS bytea
AS '${PG_PKG_LIBRARY_DIR}/${BUILD_NAME}'
LANGUAGE C STRICT IMMUTABLE;

DROP FUNCTION compile_query_int(text, bool, bool);\")"
        )
    endif(POSTGRESQL)
endif(DEFINITIONS)
