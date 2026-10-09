WIRELESS_ENABLE ?= yes

# The wireless stack is shared vendor code in keyboards/linker/wireless, the
# same directory ~20 other boards include. Split65 keeps its own copy of only
# the one file it actually changes (lpwr_wb32.c, the deep-sleep fix); every
# other source comes from the shared directory so vendor fixes reach this board
# and the divergence stays a one-file diff.
WIRELESS_DIR = $(TOP_DIR)/keyboards/linker/wireless
WIRELESS_LOCAL_DIR = $(TOP_DIR)/keyboards/epomaker/epomaker_split65/wireless

ifeq ($(strip $(WIRELESS_ENABLE)), yes)
    OPT_DEFS += -DWIRELESS_ENABLE -DNO_USB_STARTUP_CHECK

    OPT_DEFS += -include $(WIRELESS_DIR)/md_raw.h

    UART_DRIVER_REQUIRED ?= yes
    WIRELESS_LPWR_STOP_ENABLE ?= yes

    # Local dir first: lpwr_wb32.c resolves here, not in the shared directory.
    VPATH += $(WIRELESS_LOCAL_DIR) $(WIRELESS_DIR)

    SRC += \
        $(WIRELESS_DIR)/wireless.c \
        $(WIRELESS_DIR)/transport.c \
        $(WIRELESS_DIR)/lowpower.c \
        $(WIRELESS_DIR)/lowpower_logic.c \
        $(WIRELESS_DIR)/md_raw.c \
        $(WIRELESS_DIR)/smsg.c \
        $(WIRELESS_DIR)/module.c

    ifeq ($(strip $(WIRELESS_LPWR_STOP_ENABLE)), yes)
        OPT_DEFS += -DWIRELESS_LPWR_STOP_ENABLE
        SRC += $(WIRELESS_LOCAL_DIR)/lpwr_wb32.c
    endif
endif
