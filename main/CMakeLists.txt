FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "Application/Manager"
                       REQUIRES esp_http_server esp_timer esp_wifi json nvs_flash driver
                       EMBED_TXTFILES "../webserver/provision.html"
                       EMBED_TXTFILES "../webserver/logo.png"
                       EMBED_TXTFILES "../LICENSE"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    PYROVISION_VERSION_MAJOR=1
    PYROVISION_VERSION_MINOR=0
    PYROVISION_VERSION_BUILD=0
)
