foreach(component IN ITEMS Timestamp TimeZone LogLevel LogBuffer Logger FileSink RollingFileSink AsyncLogSink)
    add_executable(${component}_test ${component}_test.cc)
    target_link_libraries(${component}_test PRIVATE xmuduo)
    add_test(NAME ${component} COMMAND ${component}_test)
    set_tests_properties(${component} PROPERTIES TIMEOUT 30)
endforeach()
# Verify that explicit-zone behavior is independent of the process time zone.
add_test(NAME TimeZone_process_tz COMMAND TimeZone_test)
set_tests_properties(TimeZone_process_tz PROPERTIES
    ENVIRONMENT "TZ=Pacific/Honolulu" TIMEOUT 30)
