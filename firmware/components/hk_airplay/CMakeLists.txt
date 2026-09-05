# The AirPlay 2 receiver of ADR-0007, vendored at commit 38027441ff4327611d26153a8e8b06636cdf009f
# (upstream v0.2.0). Everything under vendor/ is upstream, byte for byte;
# everything beside it is ours. Nothing in vendor/ is patched, which is what
# makes the next upstream update a copy rather than a merge.
#
# Upstream is an application, not a library: it has its own app_main that erases
# NVS, drives an LED, brings up its own Wi-Fi and serves a captive portal. None
# of that is taken. hk_airplay.c re-implements the one function that starts the
# receiver, and this file simply does not list the sources behind the rest.
#
# The two functions the vendored sources call but whose owners we did not take
# are supplied by shim/. There are exactly two of them.

# The requirements below are listed unconditionally, and that is not a style
# choice. ESP-IDF processes a component's CMakeLists twice: an early pass that
# collects requirements, in which CONFIG_ symbols do not exist yet, and a second
# pass that builds. Wrapping idf_component_register() in `if(CONFIG_...)` means
# the early pass takes the other branch and the component's requirements are
# fixed as empty -- after which the real pass compiles the sources with no
# include paths, and the failure reads as a dozen missing system headers rather
# than as a configuration mistake. So only the source list is conditional.

set(VENDOR_SRCS
    vendor/audio/audio_receiver.c
    vendor/audio/audio_stream.c
    vendor/audio/audio_stream_realtime.c
    vendor/audio/audio_stream_buffered.c
    vendor/audio/audio_decoder.c
    vendor/audio/audio_buffer.c
    vendor/audio/audio_timing.c
    vendor/audio/audio_crypto.c
    vendor/audio/audio_resample.c
    vendor/audio/audio_output_common.c
    vendor/rtsp/rtsp_server.c
    vendor/rtsp/rtsp_conn.c
    vendor/rtsp/rtsp_message.c
    vendor/rtsp/rtsp_handlers.c
    vendor/rtsp/rtsp_fairplay.c
    vendor/rtsp/rtsp_crypto.c
    vendor/rtsp/rtsp_rsa.c
    vendor/rtsp/rtsp_events.c
    vendor/plist/plist_xml.c
    vendor/plist/bplist_parser.c
    vendor/plist/bplist_builder.c
    vendor/plist/base64.c
    vendor/hap/hap.c
    vendor/hap/hap_pair_verify.c
    vendor/hap/hap_pair_setup.c
    vendor/hap/hap_crypto.c
    vendor/hap/srp.c
    vendor/hap/tlv8.c
    vendor/network/mdns_airplay.c
    vendor/network/ptp_clock.c
    vendor/network/ntp_clock.c
    vendor/network/socket_utils.c
    vendor/settings.c
    vendor/playback_control.c
    vendor/dacp_client.c
    vendor/alac_magic_cookie.c
)

# The output backend is chosen at compile time, upstream's own arrangement:
# audio_output_common.c carries weak defaults and exactly one backend supplies
# the rest. S/PDIF is a bench output that lets the audio path be heard before
# the product's DAC exists; see Kconfig for the circuit it needs.
if(CONFIG_HK_AIRPLAY_OUTPUT_SPDIF)
    list(APPEND VENDOR_SRCS vendor/audio/audio_output_spdif.c)
else()
    list(APPEND VENDOR_SRCS vendor/audio/audio_output.c)
endif()

if(CONFIG_HK_AIRPLAY)
    set(HK_AIRPLAY_SRCS "hk_airplay.c" "shim/hk_airplay_shim.c" ${VENDOR_SRCS})
else()
    set(HK_AIRPLAY_SRCS "")
    set(VENDOR_SRCS "")
endif()

idf_component_register(
    SRCS ${HK_AIRPLAY_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor" "vendor/audio" "vendor/rtsp" "vendor/plist"
                      "vendor/hap" "vendor/network"
    REQUIRES hk_identity hk_pins hk_storage
    PRIV_REQUIRES driver nvs_flash esp_wifi esp_netif esp_event esp_ringbuf
                  esp_driver_i2s esp_timer esp_http_client esp_driver_ledc
                  app_update json mbedtls espressif__mdns dac audio-resampler
)

# Upstream's own warning posture, not this project's. The vendored tree is not
# ours to make warning-clean, and turning its warnings into errors here would
# mean patching upstream code -- exactly what vendoring at a pinned commit is
# meant to avoid. Our own two files stay under the project's rules.
set_source_files_properties(${VENDOR_SRCS}
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-function;-Wno-unused-variable")
