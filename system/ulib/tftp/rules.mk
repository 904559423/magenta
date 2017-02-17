# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
    $(LOCAL_DIR)/tftp.c

MODULE_EXPORT := so
MODULE_SO_NAME := tfp

MODULE_LIBS := \
    ulib/musl

include make/module.mk
