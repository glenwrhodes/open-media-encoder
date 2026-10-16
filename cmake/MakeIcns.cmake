# Builds a macOS .icns from a square PNG. Usage: cmake -DSRC=icon.png -DOUT=app.icns -P MakeIcns.cmake
get_filename_component(work "${OUT}" DIRECTORY)
set(iconset "${work}/OpenMediaEncoder.iconset")
file(REMOVE_RECURSE "${iconset}")
file(MAKE_DIRECTORY "${iconset}")
foreach(size 16 32 128 256 512)
    math(EXPR double "${size} * 2")
    execute_process(COMMAND sips -z ${size} ${size} "${SRC}" --out "${iconset}/icon_${size}x${size}.png"
                    OUTPUT_QUIET ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
    if(double LESS_EQUAL 512)
        execute_process(COMMAND sips -z ${double} ${double} "${SRC}" --out "${iconset}/icon_${size}x${size}@2x.png"
                        OUTPUT_QUIET ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
execute_process(COMMAND iconutil -c icns "${iconset}" -o "${OUT}" COMMAND_ERROR_IS_FATAL ANY)
