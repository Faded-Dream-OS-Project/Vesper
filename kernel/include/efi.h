/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_EFI_H
#define VESPER_EFI_H

/* Minimal EFI definitions used by the loader stub. Only what we need. */

typedef void *efi_handle;
typedef void *efi_event;
typedef u64 efi_uint_t;

struct efi_table_header {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc32;
    u32 reserved;
};

struct efi_simple_text_output_protocol {
    void *reset;
    void *output_string;   /* efi_status (*)(..., u16 *) */
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    struct {
        i32 max_mode;
        i32 mode;
        u32 attribute;
        i32 cursor_column;
        i32 cursor_row;
        void *clear_screen_visible;
    } mode;
};

struct efi_loaded_image_protocol {
    u32 revision;
    void *parent_handle;
    struct efi_system_table *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    u32 load_options_size;
    void *load_options;
    void *image_base;
    u64 image_size;
    unsigned int image_code_type;
    unsigned int image_data_type;
    void *unload;
};

struct efi_guid {
    u32 data1;
    u16 data2;
    u16 data3;
    u8  data4[8];
};

struct efi_device_path_protocol {
    u8 type;
    u8 subtype;
    u16 length;
};

#endif /* VESPER_EFI_H */
