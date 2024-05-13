idf_component_register(SRCS "connect_wifi.c" "main.c" "ws28xx/ws28xx.c"
                    INCLUDE_DIRS ".")

fatfs_create_spiflash_image(storage ../data FLASH_IN_PROJECT)
