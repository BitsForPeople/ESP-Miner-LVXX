idf_component_register(
SRCS
    "utils.c"
    "mining.c"
    "stratum_api.c"
    "strbuf.c"
    "hashpool.cpp"
    "bm_job_pool.cpp"
    "coinbase.cpp"
    "jobfactory.cpp"
    "json_rpc.cpp"
    "stratum_rpc.cpp"
                    
INCLUDE_DIRS
    "include"

REQUIRES
    "json"
    "mbedtls"
    "app_update"
    "esp_timer"
    "simd_utils"
    "objpool"
)