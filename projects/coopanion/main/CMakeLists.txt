idf_component_register(
    SRCS "main.c" "board_display.c" "coop_state.c" "coop_body.c" "coop_fx.c" "coop_hud.c" "coop_render.c" "coop_animation.c" "coop_ui.c" "coop_subtitle.c"
        "coop_board.c" "coop_link.c" "coop_audio.c" "coop_assets.c" "coop_script.c" "coop_receipt.c"
    INCLUDE_DIRS "."
    EMBED_FILES "coo-atlas.bin"
    REQUIRES esp_iris esp-mosaico-bsp esp_mosaico_app_recovery
        esp_mosaico_gsp_bundle esp_mosaico_gsp_iris freertos nvs_flash
    PRIV_REQUIRES esp_timer esp_app_format esp-gsp esp_display_present esp_lcd esp_wifi esp_netif
        esp_event esp_websocket_client cjson esp-sr esp_codec_dev esp_partition mbedtls)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json")
idf_component_get_property(esp_sr_path espressif__esp-sr COMPONENT_DIR)
set(coop_voice "${esp_sr_path}/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaole.dat")
add_custom_target(coop_voice_data ALL DEPENDS "${coop_voice}")
set_property(GLOBAL APPEND PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_LABELS voice_data)
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_IMAGE "${coop_voice}")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_voice_data_TARGET coop_voice_data)
