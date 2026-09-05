idf_component_register(SRCS "hk_identity.c"
                       INCLUDE_DIRS "include")

# The devkit reports a different OTA hardware revision so that hk_manifest refuses
# a product release on it and a devkit release on a product board. PUBLIC, not
# PRIVATE: hk_main is the file that actually reads HK_HW_REVISION, and a define
# that reached only this component would leave the header meaning two different
# things in one build - the single worst outcome available here.
if(CONFIG_HK_BOARD_DEVKIT_N8R2)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC HK_HW_REVISION="devkit-n8r2")
endif()
