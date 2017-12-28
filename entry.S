.balign 16
mb_hdr:
    .long 0x1BADB002
    .long 0x00000002
    .long -(0x1BADB002 + 0x00000002)

.balign 16
.global _start
_start:
    movl $bsp_stack_top, %esp
    push $0 // alignment
    push %ebx
    call bsp_main
1:  hlt
    jmp 1b

.balign 16
.global ap_bootstrap_trampoline_start
ap_bootstrap_trampoline_start:
    .code16
    // This is "movl $0x12345678, %ecx", with tag_offset pointing to the 32-bit immediate.
    .byte 0x66, 0xB9
.global ap_bootstrap_trampoline_itag_offset
.equ ap_bootstrap_trampoline_itag_offset, . - ap_bootstrap_trampoline_start
    .long 0x12345678
    // Save CR0 state after init to %edx.
    movl %cr0, %edx
    // All accesses are relative to CS
    mov %cs, %bx
    mov %bx, %ds
    mov %bx, %ss
    // Read dtag value
    movl (ap_bootstrap_dtag - ap_bootstrap_trampoline_start), %esi
    // Load GDT
    lgdtl ap_bootstrap_gdtr - ap_bootstrap_trampoline_start
    // Enable protected mode
    movl %edx, %eax
    orl $1, %eax
    movl %eax, %cr0
    // Jump out of the trampoline to 32-bit mode.
    ljmpl $0x08, $ap_entry
    .code32
.global ap_bootstrap_trampoline_dtag_offset
.equ ap_bootstrap_trampoline_dtag_offset, . - ap_bootstrap_trampoline_start
ap_bootstrap_dtag:
    .long 0
ap_bootstrap_gdtr:
    .word gdt_end - gdt - 1
    .long gdt
.global ap_bootstrap_trampoline_end
ap_bootstrap_trampoline_end:

.balign 16
ap_entry:
    // AP entry point. Trampoline has jumped here with
    // esi - dtag
    // ecx - itag
    // edx - initial cr0
    // cs - 32-bit code segment
    // Configure data segments & load the ap stack pointer.
    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss
    mov $ap_stack_top, %esp
    push $0 // alignment
    push %esi
    push %ecx
    push %edx
    call ap_main
1:  cli
    hlt
    jmp 1b

.section .data
.balign 16
gdt:
    .quad 0 // null segment
	.quad 0x00CF9A000000FFFF	// 08 ring 0 code
	.quad 0x00CF92000000FFFF	// 10 ring 0 data
gdt_end:

.section .bss
bsp_stack:
    .fill 0x2000
bsp_stack_top:
ap_stack:
    .fill 0x2000
ap_stack_top:
