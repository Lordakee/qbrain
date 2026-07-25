cmake_minimum_required(VERSION 3.24)
project(qbrain VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W3 /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  # Allow builds without full Windows SDK rc.exe/mt.exe (manifest tooling).
  add_link_options(/MANIFEST:NO)
  set(CMAKE_RC_COMPILER "")
  set(CMAKE_MT "")
endif()

set(SQLITE_DIR ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite-amalgamation-3460100)
set(NLOHMANN_DIR ${CMAKE_SOURCE_DIR}/third_party)

add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE_DIR})
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_ENABLE_FTS5
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
  target_compile_options(sqlite3 PRIVATE /wd4996)
endif()

function(qbrain_lib name)
  add_library(qbrain_${name} STATIC ${ARGN})
  target_include_directories(qbrain_${name}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${NLOHMANN_DIR}
  )
  target_link_libraries(qbrain_${name} PUBLIC sqlite3)
endfunction()

qbrain_lib(util
  src/qbrain/util/paths.cpp
  src/qbrain/util/hash.cpp
  src/qbrain/util/log.cpp
  src/qbrain/util/string_util.cpp
  src/qbrain/util/time_util.cpp
)

qbrain_lib(storage
  src/qbrain/storage/database.cpp
  src/qbrain/storage/migrate.cpp
)
target_link_libraries(qbrain_storage PUBLIC qbrain_util)

qbrain_lib(core
  src/qbrain/core/types.cpp
  src/qbrain/core/brain.cpp
)
target_link_libraries(qbrain_core PUBLIC qbrain_storage qbrain_util)

qbrain_lib(graph
  src/qbrain/graph/extract.cpp
  src/qbrain/graph/traverse.cpp
)
target_link_libraries(qbrain_graph PUBLIC qbrain_core qbrain_util)

qbrain_lib(search
  src/qbrain/search/vector.cpp
  src/qbrain/search/rrf.cpp
  src/qbrain/search/hybrid.cpp
)
target_link_libraries(qbrain_search PUBLIC qbrain_core qbrain_util)

qbrain_lib(ingest
  src/qbrain/ingest/chunker.cpp
  src/qbrain/ingest/markdown.cpp
  src/qbrain/ingest/import.cpp
)
target_link_libraries(qbrain_ingest PUBLIC qbrain_core qbrain_graph qbrain_util)

qbrain_lib(ai
  src/qbrain/ai/http_client.cpp
  src/qbrain/ai/embed.cpp
  src/qbrain/ai/chat.cpp
)
target_link_libraries(qbrain_ai PUBLIC qbrain_util)
if(WIN32)
  target_link_libraries(qbrain_ai PUBLIC winhttp)
endif()

qbrain_lib(ops
  src/qbrain/ops/registry.cpp
  src/qbrain/ops/handlers.cpp
)
target_link_libraries(qbrain_ops PUBLIC
  qbrain_core qbrain_search qbrain_graph qbrain_ingest qbrain_ai qbrain_util
)

qbrain_lib(mcp
  src/qbrain/mcp/jsonrpc.cpp
  src/qbrain/mcp/server.cpp
)
target_link_libraries(qbrain_mcp PUBLIC qbrain_ops qbrain_core qbrain_util)

qbrain_lib(cli
  src/qbrain/cli/app.cpp
  src/qbrain/cli/commands.cpp
)
target_link_libraries(qbrain_cli PUBLIC qbrain_mcp qbrain_ops qbrain_core qbrain_util)

add_executable(qbrain src/qbrain/main.cpp)
target_link_libraries(qbrain PRIVATE qbrain_cli qbrain_mcp qbrain_ops qbrain_core qbrain_util)
if(WIN32)
  target_link_libraries(qbrain PRIVATE bcrypt)
endif()

enable_testing()
add_executable(qbrain_tests
  tests/test_main.cpp
  tests/test_rrf.cpp
  tests/test_vector.cpp
  tests/test_chunker.cpp
  tests/test_extract.cpp
  tests/test_storage.cpp
  tests/test_mcp.cpp
)
target_link_libraries(qbrain_tests PRIVATE
  qbrain_mcp qbrain_ops qbrain_search qbrain_graph qbrain_ingest qbrain_ai
  qbrain_storage qbrain_core qbrain_util
)
if(WIN32)
  target_link_libraries(qbrain_tests PRIVATE winhttp)
endif()
if(WIN32)
  target_link_libraries(qbrain_tests PRIVATE bcrypt)
endif()
add_test(NAME qbrain_unit COMMAND qbrain_tests)

install(TARGETS qbrain RUNTIME DESTINATION bin)
