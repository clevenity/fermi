
#if defined(__linux__) && defined(__x86_64__)

.intel_syntax noprefix
.section .rodata
.Lnl:    .byte 10
.Ltrue:  .ascii "true"
.Lfalse: .ascii "false"
.section .bss
.lcomm .Libuf, 4097
.lcomm .Lfmtbuf, 40
.section .text

.Lsyswrite:
    mov     edi, 1
    mov     eax, 1
    syscall
    ret

.globl fe_print_str
.type  fe_print_str, @function
fe_print_str:
    test    rdi, rdi
    jz      .Lps_ret
    mov     rsi, rdi
    xor     edx, edx
.Lps_scan:
    cmp     byte ptr [rsi + rdx], 0
    je      .Lps_go
    inc     rdx
    jmp     .Lps_scan
.Lps_go:
    test    rdx, rdx
    jz      .Lps_ret
    mov     edi, 1
    mov     eax, 1
    syscall
.Lps_ret:
    ret
.size fe_print_str, . - fe_print_str

.globl fe_println_str
.type  fe_println_str, @function
fe_println_str:
    test    rdi, rdi
    jz      .Lpns_nl
    mov     rsi, rdi
    xor     edx, edx
.Lpns_scan:
    cmp     byte ptr [rsi + rdx], 0
    je      .Lpns_go
    inc     rdx
    jmp     .Lpns_scan
.Lpns_go:
    test    rdx, rdx
    jz      .Lpns_nl
    mov     edi, 1
    mov     eax, 1
    syscall
.Lpns_nl:
    lea     rsi, [rip + .Lnl]
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_println_str, . - fe_println_str

.globl fe_println_void
.type  fe_println_void, @function
fe_println_void:
    lea     rsi, [rip + .Lnl]
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_println_void, . - fe_println_void

.globl fe_flush
.type  fe_flush, @function
fe_flush:
    ret
.size fe_flush, . - fe_flush

.globl fe_print_bool
.type  fe_print_bool, @function
fe_print_bool:
    test    edi, edi
    jz      .Lpb_false
    lea     rsi, [rip + .Ltrue]
    mov     edx, 4
    jmp     .Lpb_write
.Lpb_false:
    lea     rsi, [rip + .Lfalse]
    mov     edx, 5
.Lpb_write:
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_print_bool, . - fe_print_bool

.globl fe_println_bool
.type  fe_println_bool, @function
fe_println_bool:
    test    edi, edi
    jz      .Lpnb_false
    lea     rsi, [rip + .Ltrue]
    mov     edx, 4
    jmp     .Lpnb_write
.Lpnb_false:
    lea     rsi, [rip + .Lfalse]
    mov     edx, 5
.Lpnb_write:
    mov     edi, 1
    mov     eax, 1
    syscall
    lea     rsi, [rip + .Lnl]
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_println_bool, . - fe_println_bool

.globl fe_print_char
.type  fe_print_char, @function
fe_print_char:
    sub     rsp, 8
    mov     [rsp], dil
    mov     rsi, rsp
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    add     rsp, 8
    ret
.size fe_print_char, . - fe_print_char

.globl fe_println_char
.type  fe_println_char, @function
fe_println_char:
    sub     rsp, 8
    mov     [rsp], dil
    mov     byte ptr [rsp + 1], 10
    mov     rsi, rsp
    mov     edx, 2
    mov     edi, 1
    mov     eax, 1
    syscall
    add     rsp, 8
    ret
.size fe_println_char, . - fe_println_char

.Lfmt_i64:
    lea     r10, [rip + .Lfmtbuf]
    lea     r11, [r10 + 39]     /* r11 = end pointer (exclusive) */
    mov     byte ptr [r11], 0

    test    rdi, rdi
    jnz     .Lfi_nz
    dec     r11
    mov     byte ptr [r11], 48  /* '0' */
    mov     rsi, r11
    lea     rdx, [r10 + 39]
    sub     rdx, r11
    ret

.Lfi_nz:
    xor     ecx, ecx            /* negative flag */
    test    rdi, rdi
    jns     .Lfi_pos
    mov     ecx, 1
    neg     rdi

.Lfi_pos:
    mov     rax, rdi
    mov     r9, 10
.Lfi_loop:
    xor     edx, edx
    div     r9
    add     dl, 48
    dec     r11
    mov     [r11], dl
    test    rax, rax
    jnz     .Lfi_loop

    test    ecx, ecx
    jz      .Lfi_done
    dec     r11
    mov     byte ptr [r11], 45  /* '-' */

.Lfi_done:
    mov     rsi, r11
    lea     rdx, [r10 + 39]
    sub     rdx, r11
    ret

.globl fe_print_i64
.type  fe_print_i64, @function
fe_print_i64:
    call    .Lfmt_i64
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_print_i64, . - fe_print_i64

.globl fe_println_i64
.type  fe_println_i64, @function
fe_println_i64:
    call    .Lfmt_i64
    mov     edi, 1
    mov     eax, 1
    syscall
    lea     rsi, [rip + .Lnl]
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_println_i64, . - fe_println_i64

.globl fe_print_i32
.type  fe_print_i32, @function
fe_print_i32:
    movsx   rdi, edi
    call    .Lfmt_i64
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_print_i32, . - fe_print_i32

.globl fe_println_i32
.type  fe_println_i32, @function
fe_println_i32:
    movsx   rdi, edi
    call    .Lfmt_i64
    mov     edi, 1
    mov     eax, 1
    syscall
    lea     rsi, [rip + .Lnl]
    mov     edx, 1
    mov     edi, 1
    mov     eax, 1
    syscall
    ret
.size fe_println_i32, . - fe_println_i32

.globl fe_input
.type  fe_input, @function
fe_input:
    lea     rsi, [rip + .Libuf]
    mov     edx, 4096
    xor     edi, edi            /* fd = stdin */
    mov     eax, 0              /* SYS_read */
    syscall
    test    rax, rax
    jle     .Linput_empty
    /* strip trailing newline */
    lea     r10, [rip + .Libuf]
    lea     r11, [r10 + rax]
    mov     byte ptr [r11], 0
    dec     r11
    cmp     byte ptr [r11], 10
    jne     .Linput_ret
    mov     byte ptr [r11], 0
    jmp     .Linput_ret
.Linput_empty:
    lea     r10, [rip + .Libuf]
    mov     byte ptr [r10], 0
.Linput_ret:
    lea     rax, [rip + .Libuf]
    ret
.size fe_input, . - fe_input

.globl fe_exit
.type  fe_exit, @function
fe_exit:
    mov     eax, 60             /* SYS_exit */
    syscall
    ud2
.size fe_exit, . - fe_exit

#endif
