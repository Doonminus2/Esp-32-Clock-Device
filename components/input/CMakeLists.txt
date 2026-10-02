idf_component_register(
    SRCS "input.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES board button
)
