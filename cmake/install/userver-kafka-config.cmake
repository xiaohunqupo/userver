include_guard(GLOBAL)

if(userver_kafka_FOUND)
  return()
endif()

find_package(userver REQUIRED COMPONENTS
  core
)

include("${USERVER_CMAKE_DIR}/modules/FindLZ4.cmake")
include("${USERVER_CMAKE_DIR}/modules/FindRdKafka.cmake")

set(userver_kafka_FOUND TRUE)
