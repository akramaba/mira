;
; Long Mode
;
; load.asm
;

[bits 16]

; Disk Address Packet for extended INT 13h
disk_address_packet:
    db 0x10         ; Size of packet is 16 bytes
    db 0            ; Reserved
.num_sectors:
    dw 0            ; Number of sectors to transfer
.buffer_addr:
    dw 0            ; Transfer buffer offset
    dw 0            ; Transfer buffer segment
.lba_start:
    dq 0            ; 64-bit starting LBA

load_bios:
    pushad

    ; For real hardware, reset the disk system for reliability
    pusha                   ; Save registers to prevent int 13h clobbering them
    mov ah, 0x00            ; Function: Reset Disk System
    mov dl, byte [boot_drive]
    int 0x13
    popa
    
    jc bios_disk_error      ; Carry set indicates error

    mov edi, edx ; Store the linear destination address in EDI

.load_loop:
    ; Check if there are any sectors left to read
    cmp cx, 0
    je .done

    ; Determine number of sectors for this chunk
    ; Maximum of 64 sectors
    mov ax, cx
    cmp ax, 64
    jbe .set_dap_sectors
    mov ax, 64
    
.set_dap_sectors:
    mov [disk_address_packet.num_sectors], ax

    ; Set up the rest of the DAP
    push eax ; Save sectors of the current chunk
    xor eax, eax
    mov ax, bx
    mov dword [disk_address_packet.lba_start], eax
    mov dword [disk_address_packet.lba_start+4], 0
    pop eax  ; Restore sectors of the current chunk

    ; Buffer Address
    mov edx, edi
    shr edx, 4  ; edx = edi / 16 (segment)
    mov [disk_address_packet.buffer_addr+2], dx
    mov dx, di
    and dx, 0x000F ; dx = edi % 16 (offset)
    mov [disk_address_packet.buffer_addr], dx

    ; Perform the BIOS disk read
    mov ah, 0x42
    mov dl, byte [boot_drive]
    mov si, disk_address_packet
    int 0x13
    jc bios_disk_error

    ; Update variables for the next iteration
    sub cx, ax
    add bx, ax

    ; Update linear destination address (EDI)
    shl ax, 9       ; ax = sectors_this_chunk * 512
    xor edx, edx    ; zero out upper bits of edx
    mov dx, ax
    add edi, edx    ; edi += bytes_read

    jmp .load_loop

.done:
    popad
    ret

bios_disk_error:
    ; Infinite loop to hang
    jmp $