idf_component_register(
    SRCS
        "main.cpp"
        "src/sensors/imu_sensor.cpp"
        "src/sensors/gps_sensor.cpp"

    INCLUDE_DIRS
        "include"

    REQUIRES
        driver
        esp_timer
)
