/* Embeds the Korean patch data (patch.bin) into the executable. */
#ifdef _WIN32
    .section .rdata,"dr"
#else
    .section .rodata
#endif
    .globl c2_blob
    .globl c2_blob_end
    .p2align 4
c2_blob:
    .incbin PATCH_BIN
c2_blob_end:
    .byte 0
#ifndef _WIN32
    .section .note.GNU-stack,"",@progbits
#endif
