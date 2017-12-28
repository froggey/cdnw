#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "cdnw.h"

static const int AP_BOOTSTRAP_LOAD_ADDRESS = 0x7000;

static int my_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *s1p = s1;
    const uint8_t *s2p = s2;
    for(size_t i = 0; i < n; i += 1) {
        if(s1p[i] != s2p[i]) {
            return s2p[i] - s1p[i];
        }
    }
    return 0;
}

static void *my_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    for(size_t i = 0; i < n; i += 1) {
        d[i] = s[i];
    }
    return dest;
}

static inline uint32_t read_lapic(uint32_t offset) {
    return *(volatile uint32_t *)(0xFEE00000 + offset);
}

static inline void write_lapic(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(0xFEE00000 + offset) = value;
}

static inline uint32_t read_cr0(void) {
    uint32_t result;
    asm volatile("movl %%cr0, %0" : "=r"(result));
    return result;
}

static inline uint32_t write_cr0(uint32_t value) {
    asm volatile("movl %0, %%cr0" :: "r"(value));
}

static inline void wbinvd(void) {
    asm volatile("wbinvd" ::: "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void compiler_barrier(void) {
    asm volatile("" ::: "memory");
}

// PIT-based delay function.

static uint16_t read_pit_count(void) {
    outb(0x43, 0);
    uint16_t lo = inb(0x40);
    uint16_t hi = inb(0x40);
    return (hi << 8) | lo;
}

static void wait_for_pit_tick(void) {
    uint16_t prev_count = read_pit_count();
    for(;;) {
        uint16_t count = read_pit_count();
        if(count > prev_count) {
            // Count has wrapped around.
            break;
        }
        prev_count = count;
    }
}

static void delay(int ms) {
    int rate = 10; // tick rate, ms
    int hz = 1000 / rate; // Hz
    int divisor = 1193180 / hz;
    outb(0x43, 0x36); // set timer to run continuously (mode 3, lobyte/hibyte)
    outb(0x40, divisor & 0xFF); // load timer low byte
    outb(0x40, divisor >> 8); // load timer high byte
    // Wait for the start of the next PIT tick.
    wait_for_pit_tick();
    for(int waited = 0; waited < ms; waited += rate) {
        wait_for_pit_tick();
    }
}

// ACPI detection.

static bool checksum_range(void *start, size_t size) {
    uint8_t sum = 0;
    uint8_t *data = (uint8_t *)start;

    for(size_t i = 0; i < size; i++) {
        sum += data[i];
    }

    return sum == 0;
}

static acpi_rsdp_t *find_acpi_rsdp_(uintptr_t start, uintptr_t end) {
    for(uintptr_t i = start; i < end; i += 16) {
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)i;
        if(my_memcmp("RSD PTR ", rsdp->signature, 8) == 0 &&
           checksum_range(rsdp, 20)) {
            return rsdp;
        }
    }

    return NULL;
}

static acpi_rsdp_t *find_acpi_rsdp(void) {
    acpi_rsdp_t *rsdp;

    /* OSPM finds the Root System Description Pointer (RSDP) structure by
     * searching physical memory ranges on 16-byte boundaries for a valid
     * Root System Description Pointer structure signature and checksum match
     * as follows:
     *   The first 1 KB of the Extended BIOS Data Area (EBDA).
     *   The BIOS read-only memory space between 0E0000h and 0FFFFFh. */

    /* Search the EBDA */
    rsdp = find_acpi_rsdp_(*(uint16_t *)0x40E << 4, 0xA0000);
    if(rsdp) {
        return rsdp;
    }

    /* Search the BIOS ROM */
    rsdp = find_acpi_rsdp_(0xE0000, 0x100000);
    if(rsdp) {
        return rsdp;
    }

    return NULL;
}

static acpi_header_t *find_acpi_table(acpi_rsdt_t *rsdt, uint8_t signature[static 4]) {
    for(size_t i = 0; i < (rsdt->header.length - sizeof(acpi_header_t)) / 4; i++) {
        acpi_header_t *table_header = (acpi_header_t *)rsdt->entries[i];
        if(my_memcmp(signature, table_header->signature, 4) == 0) {
            return table_header;
        }
    }
    return NULL;
}

extern char ap_bootstrap_trampoline_start[];
extern char ap_bootstrap_trampoline_end[];
extern char ap_bootstrap_trampoline_itag_offset[];
extern char ap_bootstrap_trampoline_dtag_offset[];

static void write_ap_bootstrap_tag(uint32_t value) {
    uintptr_t ioffset = (uintptr_t)ap_bootstrap_trampoline_itag_offset;
    *(volatile uint32_t *)(AP_BOOTSTRAP_LOAD_ADDRESS + ioffset) = value;
    uintptr_t doffset = (uintptr_t)ap_bootstrap_trampoline_dtag_offset;
    *(volatile uint32_t *)(AP_BOOTSTRAP_LOAD_ADDRESS + doffset) = value;
    asm volatile("" ::: "memory");
}

static void send_ipi(uint8_t target, uint8_t type, uint8_t vector) {
    write_lapic(0x310, (uint32_t)target << 24); // ICR high
    write_lapic(0x300, 0x4000 | ((uint32_t)type << 8) | ((uint32_t) vector));
}

static void boot_ap(uint8_t apic_id) {
    send_ipi(apic_id, 5, 0); // INIT
    delay(10); // Sleep 10ms.
    send_ipi(apic_id, 6, AP_BOOTSTRAP_LOAD_ADDRESS >> 12); // First SIPI
    delay(1); // Supposed to be 200us
    send_ipi(apic_id, 6, AP_BOOTSTRAP_LOAD_ADDRESS >> 12); // Second SIPI
    delay(1); // Supposed to be 200us
}

static int cpu_up;
static uint32_t reported_dtag;
static uint32_t reported_itag;
static uint32_t reported_cr0;
static uint32_t cr0_to_load;

// Boot an AP, load a new CR0.
// Initial CR0 will be in reported_cr0
static void test_cr0(uint8_t apic_id, uint32_t cr0) {
    cpu_up = 0;
    cr0_to_load = cr0;
    wbinvd();
    boot_ap(apic_id);
    while(!cpu_up) {
        compiler_barrier();
    }
}

// Run two sets of tests:
//
// The first set of tests checks the behaviour of CD/NW over INIT.
// CR0 is set, and the AP is rebooted to check that the CD/NW bits are preserved.
//
// The second set attempts to check cache coherency between the BSP and APs.
// The AP trampoline contains a value (the tag) which is reported back to the BSP,
// this is written, then the BSP cache is flushed, then a new value written and the AP booted.
// The APs should report the more recent (cached) value, but if they're not cache-coherent
// for some reason, they may see the earlier value.
static void test_cpu(uint8_t apic_id) {
    kprintf("%u: ", apic_id);
    // Inspect initial CR0 & clear CD/NW.
    test_cr0(apic_id, 0);
    kprintf("^*%08x^07 ", (reported_cr0 & 0x60000000) ? VGA_RED : VGA_GREEN, reported_cr0);
    // Inspect CR0 after clearing, then set CD/NW.
    test_cr0(apic_id, 0x60000010);
    kprintf("^*%08x^07 ", (reported_cr0 & 0x60000000) ? VGA_RED : VGA_GREEN, reported_cr0);
    // Inspect CR0 after setting. Leave set for next test.
    test_cr0(apic_id, 0x60000010);
    kprintf("^*%08x^07 ", (reported_cr0 & 0x60000000) ? VGA_GREEN : VGA_RED, reported_cr0);
    // Cache test with CD/NW set.
    write_ap_bootstrap_tag(0);
    cpu_up = 0;
    cr0_to_load = 0; // Clear CD/NW.
    wbinvd();
    // Tag is set to 0 here & flushed from cache. Set to 1 immediately before boot without a flush.
    write_ap_bootstrap_tag(1);
    boot_ap(apic_id);
    while(!cpu_up) {
        compiler_barrier();
    }
    kprintf("^*%x^07 ^*%x^07 ", (reported_itag == 1) ? VGA_GREEN : VGA_RED, reported_itag, (reported_dtag == 1) ? VGA_GREEN : VGA_RED, reported_dtag);
    write_ap_bootstrap_tag(0);
    cpu_up = 0;
    cr0_to_load = 0; // Clear CD/NW.
    wbinvd();
    // Tag is set to 0 here & flushed from cache. Set to 1 immediately before boot without a flush.
    write_ap_bootstrap_tag(1);
    boot_ap(apic_id);
    while(!cpu_up) {
        compiler_barrier();
    }
    kprintf("^*%x^07 ^*%x^07\n", (reported_itag == 1) ? VGA_GREEN : VGA_RED, reported_itag, (reported_dtag == 1) ? VGA_GREEN : VGA_RED, reported_dtag);
}

void ap_main(uint32_t initial_cr0, uint32_t itag, uint32_t dtag) {
    reported_itag = itag;
    reported_dtag = dtag;
    reported_cr0 = initial_cr0;
    write_cr0(cr0_to_load);
    wbinvd();
    cpu_up = 1;
}

void bsp_main(void *mb_info) {
    kprintf("\nHello, world!\n");

    acpi_rsdp_t *rsdp = find_acpi_rsdp();
    if(!rsdp) {
        kprintf("^04No ACPI RSDP detected.\n");
        return;
    }
    kprintf("^02ACPI RSDP detected at %08x with OEMID '%c%c%c%c%c%c'^07\n", rsdp,
            rsdp->OEMID[0], rsdp->OEMID[1], rsdp->OEMID[2],
            rsdp->OEMID[3], rsdp->OEMID[4], rsdp->OEMID[5]);
    // Hunt through RSDT looking for the APIC/MADT table.
    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)rsdp->rsdt_address;
    acpi_madt_t *madt = (acpi_madt_t *)find_acpi_table(rsdt, "APIC");
    if(!madt) {
        kprintf("^04No ACPI MADT table detected.\n");
        return;
    }

    int total_cpus = 0;
    int bsp_lapic_id = read_lapic(0x20) >> 24;

    kprintf("Detecting cores: ");
    for(acpi_madt_header_t *header = (acpi_madt_header_t *)madt->entries;
        header < (acpi_madt_header_t *)((uintptr_t)madt + madt->header.length);
        header = (acpi_madt_header_t *)(((uintptr_t)header) + header->length)) {
		if(header->type == 0) { /* Processor Local APIC */
			acpi_madt_lapic_t *lapic = (acpi_madt_lapic_t *)header;
			if(lapic->flags & 1) {
                kprintf("%u ", lapic->apic_id);
                total_cpus += 1;
			}
		}
	}
    kprintf("\n");
    uint32_t bsp_cr0 = read_cr0();
    kprintf("Detected %u cores. BSP has ID %u. BSP CR0 is ^*%08x^07\n", total_cpus, bsp_lapic_id, (bsp_cr0 & 0x60000000) ? VGA_RED : VGA_GREEN, bsp_cr0);

    my_memcpy((void *)AP_BOOTSTRAP_LOAD_ADDRESS,
              ap_bootstrap_trampoline_start,
              ap_bootstrap_trampoline_end - ap_bootstrap_trampoline_start);

    kprintf("Testing...\n");
    kprintf("ID Initial  Clear    Set\n");
    for(acpi_madt_header_t *header = (acpi_madt_header_t *)madt->entries;
        header < (acpi_madt_header_t *)((uintptr_t)madt + madt->header.length);
        header = (acpi_madt_header_t *)(((uintptr_t)header) + header->length)) {
		if(header->type == 0) { /* Processor Local APIC */
			acpi_madt_lapic_t *lapic = (acpi_madt_lapic_t *)header;
			if((lapic->flags & 1) && lapic->apic_id != bsp_lapic_id) {
                test_cpu(lapic->apic_id);
			}
		}
	}
    kprintf("Done.\n");

    for(;;) {}
}
