LOCAL_PATH := $(call my-dir)

common_src_files := fragment.c fatcache.c boot.c check.c dir.c fat.c main.c

common_cflags := -O2 -g -W -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64

include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(common_src_files)
LOCAL_CFLAGS := $(common_cflags)
LOCAL_C_INCLUDES := external/fsck_msdos/
LOCAL_MODULE := libfsck_msdos
LOCAL_MODULE_TAGS := optional
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := fsck_msdos.c
LOCAL_CFLAGS := $(common_cflags)
LOCAL_C_INCLUDES := external/fsck_msdos/
LOCAL_MODULE := fsck_msdos
LOCAL_MODULE_TAGS :=
LOCAL_STATIC_LIBRARIES := libfsck_msdos liblog
include $(BUILD_EXECUTABLE)
