idf_component_register(
    SRCS "net.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES timekeeping network_provisioning esp_wifi esp_netif esp_event nvs_flash lwip
)
