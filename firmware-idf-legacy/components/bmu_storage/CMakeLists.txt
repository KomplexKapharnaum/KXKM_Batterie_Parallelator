set(STORAGE_REQUIRES fatfs sdmmc nvs_flash vfs wear_levelling)

if(CONFIG_BMU_USB_MSC_ENABLED)
    list(APPEND STORAGE_REQUIRES usb tinyusb)
endif()

idf_component_register(
    SRCS "bmu_storage.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${STORAGE_REQUIRES}
)
