// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/acpi.h>

#include <acpisvc/simple.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include "acpi.h"
#include "devhost.h"

typedef struct acpi_device {
    mx_device_t* mxdev;
    char hid[8];
    acpi_handle_t handle;
    mx_device_prop_t props[2];
} acpi_device_t;

static mx_handle_t acpi_device_clone_handle(mx_device_t* dev) {
    acpi_device_t* device = dev->ctx;
    return acpi_clone_handle(&device->handle);
}

static mx_acpi_protocol_t acpi_device_acpi_proto = {
    .clone_handle = acpi_device_clone_handle,
};

static mx_status_t acpi_device_release(mx_device_t* dev) {
    acpi_device_t* device = dev->ctx;
    device_destroy(device->mxdev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t acpi_device_proto = {
    .release = acpi_device_release,
};

static mx_status_t acpi_get_child_handle_by_hid(acpi_handle_t* h, const char* hid, acpi_handle_t* child, char* child_name) {
    char name[4] = {0};
    {
        acpi_rsp_list_children_t* rsp;
        size_t len;
        mx_status_t status = acpi_list_children(h, &rsp, &len);
        if (status != NO_ERROR) {
            return status;
        }

        for (uint32_t i = 0; i < rsp->num_children; ++i) {
            if (!memcmp(rsp->children[i].hid, hid, 7)) {
                memcpy(name, rsp->children[i].name, 4);
                break;
            }
        }
        free(rsp);

        if (name[0] == 0) {
            return ERR_NOT_FOUND;
        }
    }
    if (child_name) {
        memcpy(child_name, name, 4);
    }
    return acpi_get_child_handle(h, name, child);
}

#define ACPI_HID_BATTERY "PNP0C0A"

extern mx_handle_t devhost_get_hacpi(void);

static mx_status_t acpi_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    // Find the battery device.
    // TODO(yky,teisenbe) The battery device is in _SB.PCI0 on the acer. To be replaced by real
    // acpi device publishing code.
    mx_handle_t hacpi = devhost_get_hacpi();
    if (hacpi <= 0) {
        printf("no acpi root handle\n");
        return ERR_NOT_SUPPORTED;
    }

    acpi_device_t* batt_dev = calloc(1, sizeof(acpi_device_t));
    if (!batt_dev) {
        return ERR_NO_MEMORY;
    }

    acpi_handle_t acpi_root, pcie_handle;
    acpi_handle_init(&acpi_root, hacpi);

    mx_status_t status = acpi_get_child_handle_by_hid(&acpi_root, "PNP0A08", &pcie_handle, NULL);
    acpi_handle_close(&acpi_root);
    if (status != NO_ERROR) {
        printf("no pcie handle\n");
        free(batt_dev);
        return ERR_NOT_SUPPORTED;
    }

    const char* hid = ACPI_HID_BATTERY;
    char name[4];
    status = acpi_get_child_handle_by_hid(&pcie_handle, hid, &batt_dev->handle, name);
    if (status != NO_ERROR) {
        printf("error getting battery handle %d\n", status);
        free(batt_dev);
    } else {
        memcpy(batt_dev->hid, hid, 7);
        status = device_create(name, batt_dev, &acpi_device_proto, drv, &batt_dev->mxdev);
        if (status != NO_ERROR) {
            free(batt_dev);
            goto fail;
        }
        device_set_protocol(batt_dev->mxdev, MX_PROTOCOL_ACPI, &acpi_device_acpi_proto);

        batt_dev->props[0].id = BIND_ACPI_HID_0_3;
        batt_dev->props[0].value = htobe32(*((uint32_t *)(hid)));
        batt_dev->props[1].id = BIND_ACPI_HID_4_7;
        batt_dev->props[1].value = htobe32(*((uint32_t *)(hid + 4)));

        status = device_add_with_props(batt_dev->mxdev, dev, batt_dev->props,
                                       countof(batt_dev->props));
        if (status != NO_ERROR) {
            device_destroy(batt_dev->mxdev);
            free(batt_dev);
            goto fail;
        }
    }

fail:
    acpi_handle_close(&pcie_handle);
    return status;
}

void devhost_launch_devhost(mx_device_t* parent, const char* name, uint32_t protocol_id, const char* procname, int argc, char** argv);

static mx_status_t acpi_root_init(mx_driver_t* driver) {
    // launch the acpi devhost
    char arg1[20];
    snprintf(arg1, sizeof(arg1), "acpi");
    const char* args[2] = { "/boot/bin/devhost", arg1};
    devhost_launch_devhost(driver_get_root_device(), "acpi", MX_PROTOCOL_ACPI_BUS, "devhost:acpi", 2, (char**)args);
    return NO_ERROR;
}

static mx_driver_ops_t acpi_root_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = acpi_root_init,
};

MAGENTA_DRIVER_BEGIN(acpi_root, acpi_root_driver_ops, "magenta", "0.1", 0)
MAGENTA_DRIVER_END(acpi_root)

static mx_driver_ops_t acpi_driver_ops = {
    .bind = acpi_bind,
};

MAGENTA_DRIVER_BEGIN(acpi, acpi_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ACPI_BUS),
MAGENTA_DRIVER_END(acpi)
