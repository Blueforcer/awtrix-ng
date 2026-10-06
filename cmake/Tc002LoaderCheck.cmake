# cmake -DREADELF=... -DLOADER=libawtrix-loader.so -P Tc002LoaderCheck.cmake
#
# zkgui resolves exactly its three entry points in the loader, and the stock userspace offers
# libc.so.6 and libdl.so.2 with symbol versions up to GLIBC_2.30; a loader that exports more or
# needs more is refused.
execute_process(COMMAND "${READELF}" -W --dyn-syms "${LOADER}" OUTPUT_VARIABLE symbols COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${READELF}" -W -d "${LOADER}" OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${READELF}" -W -V "${LOADER}" OUTPUT_VARIABLE versions COMMAND_ERROR_IS_FATAL ANY)

set(exports)
string(REGEX MATCHALL "GLOBAL +[A-Z]+ +[0-9A-Z]+ +[^ \n]+" globals "${symbols}")
foreach(entry IN LISTS globals)
  if(NOT entry MATCHES " UND ")
    string(REGEX REPLACE ".* " "" name "${entry}")
    list(APPEND exports "${name}")
  endif()
endforeach()
list(SORT exports)
if(NOT exports STREQUAL "onEasyUIDeinit;onEasyUIInit;onStartupApp")
  message(FATAL_ERROR "${LOADER} exports '${exports}', not the three zkgui entry points")
endif()

set(needed)
string(REGEX MATCHALL "\\(NEEDED\\)[^[\n]*\\[[^]\n]+\\]" entries "${dynamic}")
foreach(entry IN LISTS entries)
  string(REGEX REPLACE ".*\\[(.*)\\]" "\\1" library "${entry}")
  list(APPEND needed "${library}")
endforeach()
list(SORT needed)
if(NOT needed STREQUAL "libc.so.6;libdl.so.2")
  message(FATAL_ERROR "${LOADER} needs '${needed}', not libc.so.6 and libdl.so.2")
endif()

string(REGEX MATCHALL "GLIBC_[0-9.]+" glibc "${versions}")
set(newest 0)
foreach(version IN LISTS glibc)
  string(REPLACE "GLIBC_" "" version "${version}")
  if(version VERSION_GREATER newest)
    set(newest "${version}")
  endif()
endforeach()
if(newest VERSION_GREATER 2.30)
  message(FATAL_ERROR "${LOADER} needs GLIBC_${newest}, newer than the stock GLIBC_2.30")
endif()
