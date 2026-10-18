set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c)

idf_component_register(SRCS "main.c"
                            "myui/myui.c"
                            "LCD/LCD.c"
                            ${UI_SRCS}
                    INCLUDE_DIRS ""
                                ${UI_DIR}
                                "myui"
                                "LCD"
                                )

set_source_files_properties(LCD/LCD.c PROPERTIES COMPILE_OPTIONS "-Wno-missing-braces")
