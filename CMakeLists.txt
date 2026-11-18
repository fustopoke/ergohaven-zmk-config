zephyr_library()

zephyr_library_sources_ifdef(CONFIG_K04_STATUS_LED src/status_led.c)
