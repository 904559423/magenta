# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

DRIVER_SRCS += \
    $(LOCAL_DIR)/usb-virtual-bus.c \
    $(LOCAL_DIR)/usb-virtual-client.c \
    $(LOCAL_DIR)/usb-virtual-hci.c \
    $(LOCAL_DIR)/util.c \
