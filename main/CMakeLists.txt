# Didf_component_register(SRCS "main.c"
                            "obd_can.c"
                            "obd_parser.c" 
                            "api_client.c"
                            "wifi_manager.c"
                    INCLUDE_DIRS ".")ine as dependências do nosso componente principal.
set(requires "esp_http_server" "esp_wifi" "nvs_flash" "spiffs" "driver" "esp_timer")
# Registra o componente.
idf_component_register(SRCS "main.c"
                   INCLUDE_DIRS "."
                   REQUIRES ${requires})