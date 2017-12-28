#pragma once

#define VGA_RED 0x04
#define VGA_GREEN 0x02

// Simple printf-like printing.
// Extended with syntax for setting the text colour.
// ^nn where nn are two hex digits will set the current vga attribute byte to that value.
// ^* will read the next int from the argument list & set the attribute to that.
extern void kprintf(const char *control, ...);

typedef struct acpi_rsdp {
    uint8_t signature[8];    /* "RSD PTR "*/
    uint8_t checksum;
    uint8_t OEMID[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char OEMID[6];
    char OEM_table_id[8];
    uint32_t OEM_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
} __attribute__((packed)) acpi_header_t;

typedef struct acpi_rsdt {
    acpi_header_t header;    /* Signature is 'RSDT' */
    uint32_t entries[];
} __attribute__((packed)) acpi_rsdt_t;

/* An array of APIC structures follows */
typedef struct acpi_madt {
    acpi_header_t header;    /* Signature is 'APIC' */
    uint32_t lapic_addr;     /* The 32-bit physical address at which each processor can access its local APIC. */
    uint32_t flags;
    char entries[];
} __attribute__((packed)) acpi_madt_t;

/* APIC structure types:
 * 0 Processor Local APIC
 * 1 I/O APIC
 * 2 Interrupt Source Override
 * 3 Non-maskable Interrupt Source (NMI)
 * 4 Local APIC NMI Structure
 * 5 Local APIC Address Override Structure
 * 6 I/O SAPIC
 * 7 Local SAPIC
 * 8 Platform Interrupt Sources */

/* MADT entry header */
typedef struct acpi_madt_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_header_t;

/* Processor Local APIC */
typedef struct acpi_madt_lapic {
    acpi_madt_header_t header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;        /* Bit 0 - Enabled */
} __attribute__((packed)) acpi_madt_lapic_t;
