; * Rewritten on 8/26/2025 to open up to 4GB of memory
; * for the kernel to use, including the framebuffer.
; * Additionally, the PDT information now starts at
; * 0x900000 (+8MB) to prevent general memory conflicts.
; * Lastly, rewriting pdt.asm helped as a refresher
; * and a way to improve my understanding of paging.

[bits 32]

; Initialize the page table that identity‑maps the first 4 GiB.
; Currently, user space is also mapped.

init_pt_protected:
    pushad

    ; We need space for: PML4 (4K), PDPT (4K), 4x PDs (16K)
    ; Start at 0x900000, which is safe.
    mov eax, 0x900000
    mov cr3, eax

    ; Clear all page table memory
    xor eax, eax
    mov edi, 0x900000
    mov ecx, 6 * 1024               ; 24 KiB for all 6 tables
    rep stosd

    ; Link PML4[0] to PDPT
    mov dword [0x900000], 0x901007  ; PDPT @ 0x901000 + Flags

    ; Link PDPT[0-3] to the four Page Directories (1GB each)
    mov dword [0x901000], 0x902007  ; PD0 @ 0x902000 (0-1GB)
    mov dword [0x901008], 0x903007  ; PD1 @ 0x903000 (1-2GB)
    mov dword [0x901010], 0x904007  ; PD2 @ 0x904000 (2-3GB)
    mov dword [0x901018], 0x905007  ; PD3 @ 0x905000 (3-4GB)

    ; Fill Page Directories to Map 4 GiB
    mov edi, 0x902000               ; Start with PD0
    mov ebx, 0x00000083             ; ? Flags: Present, R/W, 2MiB Page Each, Kernel Only
    mov ecx, 4 * 512                ; 4 Page Directories * 512 entries each
.map_loop:
    mov [edi], ebx
    add ebx, 0x200000               ; Next 2 MiB physical block
    add edi, 8                      ; Next 64-bit entry
    loop .map_loop

    ; Enable PAE (64‑bit long mode needs it)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; All set – higher layers enable long mode & paging later.
    popad
    ret