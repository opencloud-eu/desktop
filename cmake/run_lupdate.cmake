# Script that runs lupdate with the correct parameters. Meant to be called with cmake -P
# Expected parameters:
#   LUPDATE    - path to the lupdate executable
#   TS_FILE    - output .ts file
#   SRC_DIR    - root directory of the source tree
#   WAE        - enable the -warnings-are-errors switch for testing

set(command ${LUPDATE} src -no-obsolete -ts ${TS_FILE} -I src)
if(WAE)
    string(APPEND command " -warnings-are-errors")
endif()

message(${command})
execute_process(
    COMMAND ${command}
    WORKING_DIRECTORY ${SRC_DIR}
)