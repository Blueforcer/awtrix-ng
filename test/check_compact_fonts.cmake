execute_process(COMMAND "${NM}" --demangle --defined-only "${BINARY}"
  RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE error)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Cannot inspect compact font tables: ${error}")
endif()
foreach(name kMatrixLight6Glyphs kMatrixChunky8x6Glyphs)
  if(NOT symbols MATCHES "awtrix::${name}([\r\n]|$)")
    message(FATAL_ERROR "Compact font table missing: ${name}")
  endif()
endforeach()
foreach(name kMatrixChunky6Glyphs kMatrixChunky6XGlyphs kMatrixLight6XGlyphs
             kMatrixChunky8Glyphs kMatrixChunky8XGlyphs kMatrixLight8Glyphs
             kMatrixLight8XGlyphs kMatrixLight8x6Glyphs)
  if(symbols MATCHES "awtrix::${name}([\r\n]|$)")
    message(FATAL_ERROR "Removed font table remains in compact binary: ${name}")
  endif()
endforeach()
