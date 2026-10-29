; Точка входа ядра. GRUB/QEMU загружает нас по стандарту Multiboot.
MB_MAGIC equ 0x1BADB002
MB_FLAGS equ 0x3                    ; выравнивание модулей + карта памяти
MB_CHECK equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECK

section .bss
align 16
stack_bottom:
    resb 16384                      ; 16 КБ стека
stack_top:

section .text
global _start
extern kernel_main
_start:
    mov esp, stack_top
    push ebx                        ; адрес структуры multiboot info
    push eax                        ; magic-число от загрузчика
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang
