VERSION_STRING = 3.1.0

# PRODUCT_FIRMWARE_VERSION reported by default
# FIXME: Unclear if this is used, PRODUCT_FIRMWARE_VERSION defaults to 65535 every release
VERSION = 3103

CFLAGS += -DSYSTEM_VERSION_STRING=$(VERSION_STRING)
