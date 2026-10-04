; BOXTEST - DOS test fixture for the AROS port of Boxer.
; Original code of the aros-boxerapp project; no third-party code.
;
; 16-bit real-mode .COM for DOSBox 0.74 and later. Every mode writes a
; plain-ASCII CRLF log in the current directory so that automated tests
; compare files instead of reading the screen. Formats and errorlevels are
; specified in README.md; change them only together with that file and
; the files under expected/.
;
; Build: ./build.sh  (nasm -f bin)

        cpu     8086
        org     100h

LOGMAX          equ 8000        ; bytes of log kept in memory
LOGSLACK        equ 64          ; room for the closing END line
IDLE_TICKS      equ 1092        ; 60 s at 18.2065 Hz: interactive modes end by themselves
ADLIB_TICKS     equ 637         ; 35 s at 18.2065 Hz
NOTE_TICKS      equ 18          ; about 1 s per note
SB_WAIT_TICKS   equ 36          ; about 2 s for the DMA-complete IRQ
SB_LEN          equ 4000        ; 0.5 s at 8000 Hz
SB_TC           equ 131         ; 256 - 1000000/8000
TIME_WINDOW     equ 18          ; ticks per TIME line (about 1 s)
TIME_LINES      equ 280         ; TIME ends by itself after this many lines
SAVE_LEN        equ 14          ; magic(8) + counter(4) + checksum(2)

start:
        cld
        call    parse_arg
        mov     si, modetab
.next:
        mov     di, [si]
        or      di, di
        jz      usage
        push    si
        mov     si, argbuf
        call    strcmp
        pop     si
        je      .found
        add     si, 4
        jmp     .next
.found:
        call    [si+2]
        jmp     exit
usage:
        mov     dx, usage_msg
        mov     ah, 09h
        int     21h
        mov     al, 1
        call    setrc
exit:
        mov     al, [exitcode]
        mov     ah, 4Ch
        int     21h

; ---------------------------------------------------------------- helpers

; Raise the exit code to AL; the highest failure wins (see README).
setrc:
        cmp     al, [exitcode]
        jbe     .r
        mov     [exitcode], al
.r:     ret

; First command-line word, upper-cased, at most 8 chars, into argbuf.
parse_arg:
        mov     si, 81h
        mov     cl, [80h]
        xor     ch, ch
        mov     bx, si
        add     bx, cx
        mov     di, argbuf
.skip:
        cmp     si, bx
        jae     .done
        lodsb
        cmp     al, ' '
        je      .skip
        cmp     al, 9
        je      .skip
        dec     si
        mov     cx, 8
.copy:
        cmp     si, bx
        jae     .done
        lodsb
        cmp     al, ' '
        jbe     .done
        cmp     al, 'a'
        jb      .st
        cmp     al, 'z'
        ja      .st
        sub     al, 20h
.st:    stosb
        loop    .copy
.done:  mov     byte [di], 0
        ret

; Compare zero-terminated SI and DI; ZF=1 when equal.
strcmp:
        mov     al, [si]
        cmp     al, [di]
        jne     .r
        inc     si
        inc     di
        or      al, al
        jnz     strcmp
.r:     ret

; DX:AX = BIOS tick count at 0040:006C, read atomically.
get_ticks:
        push    ds
        xor     ax, ax
        mov     ds, ax
        cli
        mov     ax, [046Ch]
        mov     dx, [046Eh]
        sti
        pop     ds
        ret

print_dollar:                   ; DX = '$'-terminated string
        mov     ah, 09h
        int     21h
        ret

; ---------------------------------------------------------------- log

; Start a new log; SI = mode name. Writes "BOXTEST 1.0 <MODE>".
log_begin:
        mov     word [logpos], 0
        mov     word [linestart], 0
        push    si
        mov     si, s_header
        call    log_str
        pop     si
        call    log_str
        jmp     log_crlf

log_ch:                         ; AL
        push    di
        mov     di, [logpos]
        cmp     di, LOGMAX
        jae     .full
        mov     [logbuf+di], al
        inc     word [logpos]
.full:  pop     di
        ret

log_str:                        ; SI, zero-terminated
        lodsb
        or      al, al
        jz      .r
        call    log_ch
        jmp     log_str
.r:     ret

log_line:                       ; SI, then CRLF
        call    log_str
        ; fall through
log_crlf:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     al, 13
        call    log_ch
        mov     al, 10
        call    log_ch
        ; Echo the finished line so a person watching sees the same text.
        mov     dx, [linestart]
        mov     cx, [logpos]
        sub     cx, dx
        add     dx, logbuf
        mov     bx, 1
        mov     ah, 40h
        int     21h
        mov     ax, [logpos]
        mov     [linestart], ax
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

log_space:
        mov     al, ' '
        jmp     log_ch

log_hex8:                       ; AL
        push    ax
        push    cx
        push    ax
        mov     cl, 4
        shr     al, cl
        call    .nib
        pop     ax
        and     al, 0Fh
        call    .nib
        pop     cx
        pop     ax
        ret
.nib:   cmp     al, 10
        jb      .d
        add     al, 7
.d:     add     al, '0'
        jmp     log_ch

log_hex16:                      ; AX
        push    ax
        mov     al, ah
        call    log_hex8
        pop     ax
        jmp     log_hex8

log_hex32:                      ; DX:AX
        push    ax
        mov     ax, dx
        call    log_hex16
        pop     ax
        jmp     log_hex16

; Write the log to the file named at DX (create/truncate). Errorlevel 2 on failure.
log_flush:
        mov     ah, 3Ch
        xor     cx, cx
        int     21h
        jc      .err
        mov     bx, ax
        mov     ah, 40h
        mov     cx, [logpos]
        mov     dx, logbuf
        int     21h
        jc      .errc
        cmp     ax, cx
        jne     .errc
        mov     ah, 3Eh
        int     21h
        jc      .err
        ret
.errc:  mov     ah, 3Eh
        int     21h
.err:   mov     dx, m_ioerr
        call    print_dollar
        mov     al, 2
        jmp     setrc

; True (CF=1) when the log is nearly full; the caller ends the mode.
log_nearly_full:
        cmp     word [logpos], LOGMAX-LOGSLACK
        cmc
        ret

; ---------------------------------------------------------------- KEYS

mode_keys:
        mov     si, n_keys
        call    log_begin
        mov     dx, m_keys
        call    print_dollar
        call    get_ticks
        mov     [idle], ax
.loop:
        call    log_nearly_full
        jc      .full
        mov     ah, 11h
        int     16h
        jnz     .key
        call    get_ticks
        sub     ax, [idle]
        cmp     ax, IDLE_TICKS
        jb      .loop
        mov     si, s_end_timeout
        call    log_line
        mov     al, 5
        call    setrc
        jmp     .out
.key:
        mov     ah, 10h         ; enhanced read: arrows keep their E0 prefix
        int     16h
        mov     [lastk], ax
        mov     al, 'K'
        call    log_ch
        call    log_space
        mov     al, [lastk+1]
        call    log_hex8
        call    log_space
        mov     al, [lastk]
        call    log_hex8
        call    log_crlf
        cmp     word [lastk], 011Bh
        je      .esc
        call    get_ticks
        mov     [idle], ax
        jmp     .loop
.esc:   mov     si, s_end_esc
        call    log_line
        jmp     .out
.full:  mov     si, s_end_full
        call    log_line
        mov     al, 6
        call    setrc
.out:   mov     dx, f_keys
        jmp     log_flush

; ---------------------------------------------------------------- MOUSE

mode_mouse:
        mov     si, n_mouse
        call    log_begin
        xor     ax, ax
        int     33h
        cmp     ax, 0FFFFh
        je      .present
        mov     si, s_mabsent
        call    log_line
        mov     al, 3
        call    setrc
        jmp     .flush
.present:
        mov     [lastk], bx
        mov     si, s_mpresent
        call    log_str
        mov     ax, [lastk]
        call    log_hex16
        call    log_crlf
        mov     dx, m_mouse
        call    print_dollar
        mov     ax, 1
        int     33h
        mov     word [lastb], 0FFFFh    ; forces the initial state to be logged
        call    get_ticks
        mov     [idle], ax
.loop:
        call    log_nearly_full
        jc      .full
        mov     ax, 3
        int     33h
        cmp     cx, [lastx]
        jne     .chg
        cmp     dx, [lasty]
        jne     .chg
        cmp     bx, [lastb]
        je      .nochg
.chg:
        mov     [lastx], cx
        mov     [lasty], dx
        mov     [lastb], bx
        mov     al, 'M'
        call    log_ch
        call    log_space
        mov     ax, [lastx]
        call    log_hex16
        call    log_space
        mov     ax, [lasty]
        call    log_hex16
        call    log_space
        mov     al, [lastb]
        call    log_hex8
        call    log_crlf
        call    get_ticks
        mov     [idle], ax
        test    byte [lastb], 2
        jnz     .right
.nochg:
        mov     ah, 11h
        int     16h
        jz      .tm
        mov     ah, 10h
        int     16h
        cmp     al, 1Bh
        je      .esc
        call    get_ticks       ; other keys are consumed but count as activity
        mov     [idle], ax
        jmp     .loop
.tm:    call    get_ticks
        sub     ax, [idle]
        cmp     ax, IDLE_TICKS
        jb      .loop
        mov     si, s_end_timeout
        call    log_line
        mov     al, 5
        call    setrc
        jmp     .hide
.right: mov     si, s_end_right
        call    log_line
        jmp     .hide
.esc:   mov     si, s_end_esc
        call    log_line
        jmp     .hide
.full:  mov     si, s_end_full
        call    log_line
        mov     al, 6
        call    setrc
.hide:  mov     ax, 2
        int     33h
.flush: mov     dx, f_mouse
        jmp     log_flush

; ---------------------------------------------------------------- ADLIB

adlib_write:                    ; AH = register, AL = value
        push    ax
        push    cx
        push    dx
        mov     dx, 388h
        xchg    al, ah
        out     dx, al
        ; OPL2 needs 3.3 us after the index and 23 us after the data;
        ; reading the status port is the documented way to wait.
        mov     cx, 6
.d1:    in      al, dx
        loop    .d1
        inc     dx
        mov     al, ah
        out     dx, al
        dec     dx
        mov     cx, 35
.d2:    in      al, dx
        loop    .d2
        pop     dx
        pop     cx
        pop     ax
        ret

; Timer-based detection. CF=1 when absent.
adlib_detect:
        mov     ax, 0460h
        call    adlib_write
        mov     ax, 0480h
        call    adlib_write
        mov     dx, 388h
        in      al, dx
        mov     bl, al
        mov     ax, 02FFh
        call    adlib_write
        mov     ax, 0421h
        call    adlib_write
        mov     cx, 200         ; >80 us for timer 1 to overflow
.w:     in      al, dx
        loop    .w
        in      al, dx
        mov     bh, al
        mov     ax, 0460h
        call    adlib_write
        mov     ax, 0480h
        call    adlib_write
        and     bl, 0E0h
        jnz     .absent
        and     bh, 0E0h
        cmp     bh, 0C0h
        jne     .absent
        clc
        ret
.absent:
        stc
        ret

adlib_note:                     ; AL = note index 0..7, channel 0, block 5
        xor     ah, ah
        mov     si, ax
        shl     si, 1
        mov     bx, [fnums+si]
        mov     ax, 0B000h      ; key off first so every note re-attacks
        call    adlib_write
        mov     ah, 0A0h
        mov     al, bl
        call    adlib_write
        mov     ah, 0B0h
        mov     al, bh
        or      al, 34h         ; key on | block 5 << 2
        jmp     adlib_write

mode_adlib:
        mov     si, n_adlib
        call    log_begin
        call    adlib_detect
        jnc     .present
        mov     si, s_aabsent
        call    log_line
        mov     al, 3
        call    setrc
        jmp     .flush
.present:
        mov     si, s_apresent
        call    log_line
        mov     si, adlib_init
.init:  lodsw
        or      ax, ax
        jz      .go
        call    adlib_write
        jmp     .init
.go:
        mov     byte [curnote], 0FFh
        mov     word [notes], 0
        call    get_ticks
        mov     [t0], ax
        mov     [t0+2], dx
        mov     si, s_start
        call    log_str
        mov     ax, [t0]
        mov     dx, [t0+2]
        call    log_hex32
        call    log_crlf
.play:
        ; A 16-bit difference is enough for 637 ticks and also survives
        ; the midnight reset of the counter only if it does not occur
        ; during the run; README notes this.
        call    get_ticks
        sub     ax, [t0]
        cmp     ax, ADLIB_TICKS
        jae     .done
        mov     bl, NOTE_TICKS
        div     bl
        and     al, 7
        cmp     al, [curnote]
        je      .play
        mov     [curnote], al
        inc     word [notes]
        call    adlib_note
        jmp     .play
.done:
        mov     [t1], ax
        mov     ax, 0B000h
        call    adlib_write
        call    get_ticks
        mov     [t1+2], ax      ; keep low word of end tick for ELAPSED
        push    ax
        mov     si, s_endtick
        call    log_str
        pop     ax
        call    log_hex32
        call    log_crlf
        mov     si, s_elapsed
        call    log_str
        mov     ax, [t1+2]
        sub     ax, [t0]
        call    log_hex16
        call    log_crlf
        mov     si, s_notes
        call    log_str
        mov     ax, [notes]
        call    log_hex16
        call    log_crlf
.flush:
        mov     dx, f_adlib
        call    log_flush
        jmp     sb_test

; ---------------------------------------------------------------- SB

; Parse BLASTER=Axxx Ix Dx from the environment; defaults stay otherwise.
parse_blaster:
        push    es
        mov     es, [2Ch]
        xor     di, di
.var:
        cmp     byte [es:di], 0
        je      .out
        mov     si, s_blaster
        mov     bx, di
.cmp:
        lodsb
        or      al, al
        jz      .match
        cmp     al, [es:di]
        jne     .nom
        inc     di
        jmp     .cmp
.nom:   mov     di, bx
.sk:    mov     al, [es:di]
        inc     di
        or      al, al
        jnz     .sk
        jmp     .var
.match:
        mov     byte [sb_src], 1
        mov     dl, 1           ; DL=1: at the start of a token
.tok:
        mov     al, [es:di]
        or      al, al
        jz      .out
        inc     di
        cmp     al, ' '
        jne     .nsp
        mov     dl, 1
        jmp     .tok
.nsp:
        or      dl, dl
        jz      .tok
        xor     dl, dl
        and     al, 0DFh        ; upper-case letters
        cmp     al, 'A'
        jne     .ni
        call    parse_hex
        mov     [sb_base], ax
        jmp     .tok
.ni:    cmp     al, 'I'
        jne     .nd
        call    parse_dec
        mov     [sb_irq], al
        jmp     .tok
.nd:    cmp     al, 'D'
        jne     .tok
        call    parse_dec
        mov     [sb_dma], al
        jmp     .tok
.out:   pop     es
        ret

parse_hex:                      ; ES:DI -> AX
        xor     ax, ax
.l:     mov     bl, [es:di]
        sub     bl, '0'
        cmp     bl, 9
        jbe     .dg
        and     bl, 0DFh        ; 'a'-'0' -> 'A'-'0'
        sub     bl, 'A'-'0'
        cmp     bl, 5
        ja      .r
        add     bl, 10
.dg:    mov     cl, 4
        shl     ax, cl
        or      al, bl
        inc     di
        jmp     .l
.r:     ret

parse_dec:                      ; ES:DI -> AX
        xor     ax, ax
.l:     mov     bl, [es:di]
        sub     bl, '0'
        cmp     bl, 9
        ja      .r
        mov     cx, ax
        shl     ax, 1
        shl     ax, 1
        add     ax, cx
        shl     ax, 1
        add     al, bl
        adc     ah, 0
        inc     di
        jmp     .l
.r:     ret

dsp_write:                      ; AL; ignores a busy timeout (the read will fail)
        push    ax
        push    cx
        push    dx
        mov     ah, al
        mov     dx, [sb_base]
        add     dx, 0Ch
        mov     cx, 0FFFFh
.w:     in      al, dx
        test    al, 80h
        jz      .ok
        loop    .w
.ok:    mov     al, ah
        out     dx, al
        pop     dx
        pop     cx
        pop     ax
        ret

dsp_read:                       ; AL; CF=1 on timeout
        push    cx
        push    dx
        mov     dx, [sb_base]
        add     dx, 0Eh
        mov     cx, 0FFFFh
.w:     in      al, dx
        test    al, 80h
        jnz     .rd
        loop    .w
        stc
        jmp     .r
.rd:    sub     dx, 4           ; base+0Ah
        in      al, dx
        clc
.r:     pop     dx
        pop     cx
        ret

dsp_reset:                      ; CF=1 on failure
        mov     dx, [sb_base]
        add     dx, 6
        mov     al, 1
        out     dx, al
        mov     cx, 16          ; >3 us
.d:     in      al, dx
        loop    .d
        xor     al, al
        out     dx, al
        mov     cx, 100
.t:     call    dsp_read
        jc      .f
        cmp     al, 0AAh
        je      .ok
        loop    .t
.f:     stc
        ret
.ok:    clc
        ret

sb_isr:
        push    ax
        push    dx
        mov     dx, [cs:sb_base]
        add     dx, 0Eh
        in      al, dx          ; acknowledges the 8-bit DMA interrupt on the DSP
        mov     byte [cs:irqflag], 1
        mov     al, 20h
        out     20h, al
        pop     dx
        pop     ax
        iret

; Physical address of DS:SI -> BL = page, AX = low 16 bits.
phys_addr:
        mov     ax, ds
        mov     bx, ax
        mov     cl, 4
        shl     ax, cl
        mov     cl, 12
        shr     bx, cl
        add     ax, si
        adc     bl, 0
        ret

; 8-bit single-cycle DMA of a 500 Hz square wave; sets irqflag on completion.
; CF=1 when the configuration cannot be handled (IRQ outside 3..7 on the
; master PIC, or DMA channel above 3).
sb_play:
        cmp     byte [sb_irq], 3
        jb      .unsup
        cmp     byte [sb_irq], 7
        ja      .unsup
        cmp     byte [sb_dma], 3
        jbe     .ok
.unsup: stc
        ret
.ok:
        ; The 8237 cannot cross a 64 KB physical page, so the buffer is
        ; twice the length and the half that does not cross is used.
        mov     si, dmabuf
        call    phys_addr
        add     ax, SB_LEN-1
        jnc     .fits
        mov     si, dmabuf+SB_LEN
.fits:
        mov     [dmaoff], si
        mov     di, si
        xor     bx, bx
.fill:  mov     al, 40h
        test    bl, 8
        jz      .lo
        mov     al, 0C0h
.lo:    stosb
        inc     bx
        cmp     bx, SB_LEN
        jb      .fill

        mov     byte [irqflag], 0
        mov     al, [sb_irq]
        add     al, 8
        mov     [sb_vec], al
        mov     ah, 35h
        int     21h
        mov     [oldvec], bx
        mov     [oldvec+2], es
        push    ds
        pop     es
        mov     al, [sb_vec]
        mov     dx, sb_isr
        mov     ah, 25h
        int     21h
        in      al, 21h
        mov     [picsave], al
        mov     cl, [sb_irq]
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah
        out     21h, al

        mov     cl, [sb_dma]
        mov     al, cl
        or      al, 4
        out     0Ah, al         ; mask channel
        out     0Ch, al         ; clear byte flip-flop (value ignored)
        mov     al, 48h         ; single mode, increment, memory -> device
        or      al, cl
        out     0Bh, al
        mov     si, [dmaoff]
        call    phys_addr
        mov     [pagebyte], bl
        xor     dh, dh
        mov     dl, [sb_dma]
        shl     dl, 1
        out     dx, al
        mov     al, ah
        out     dx, al
        inc     dx
        mov     ax, SB_LEN-1
        out     dx, al
        mov     al, ah
        out     dx, al
        mov     bl, [sb_dma]
        xor     bh, bh
        mov     dl, [dmapage+bx]
        mov     al, [pagebyte]
        out     dx, al
        mov     al, [sb_dma]
        out     0Ah, al         ; unmask

        mov     al, 0D1h        ; speaker on
        call    dsp_write
        mov     al, 40h
        call    dsp_write
        mov     al, SB_TC
        call    dsp_write
        mov     al, 14h
        call    dsp_write
        mov     al, (SB_LEN-1) & 0FFh
        call    dsp_write
        mov     al, (SB_LEN-1) >> 8
        call    dsp_write

        call    get_ticks
        mov     [t0], ax
.wait:  cmp     byte [irqflag], 0
        jne     .done
        call    get_ticks
        sub     ax, [t0]
        cmp     ax, SB_WAIT_TICKS
        jb      .wait
        mov     al, 0D0h        ; halt DMA so nothing fires after the vector is restored
        call    dsp_write
.done:
        mov     al, 0D3h        ; speaker off
        call    dsp_write
        mov     al, [sb_dma]
        or      al, 4
        out     0Ah, al
        cli
        mov     al, [picsave]
        out     21h, al
        sti
        push    ds
        mov     al, [sb_vec]
        lds     dx, [oldvec]
        mov     ah, 25h
        int     21h
        pop     ds
        clc
        ret

sb_test:
        mov     si, n_sb
        call    log_begin
        call    parse_blaster
        mov     si, s_cfg_a
        call    log_str
        mov     ax, [sb_base]
        call    log_hex16
        mov     si, s_cfg_i
        call    log_str
        mov     al, [sb_irq]
        call    log_hex8
        mov     si, s_cfg_d
        call    log_str
        mov     al, [sb_dma]
        call    log_hex8
        mov     si, s_src_def
        cmp     byte [sb_src], 0
        je      .src
        mov     si, s_src_env
.src:   call    log_line

        call    dsp_reset
        jnc     .rok
        mov     si, s_rfail
        call    log_line
        mov     si, s_pskip
        call    log_line
        mov     al, 3
        call    setrc
        jmp     .flush
.rok:   mov     si, s_rok
        call    log_line
        mov     al, 0E1h
        call    dsp_write
        call    dsp_read
        jc      .vfail
        mov     [lastk+1], al
        call    dsp_read
        jc      .vfail
        mov     [lastk], al
        mov     si, s_ver
        call    log_str
        mov     ax, [lastk]
        call    log_hex16
        call    log_crlf
        jmp     .play
.vfail: mov     si, s_vfail
        call    log_line
        mov     al, 3
        call    setrc
.play:
        call    sb_play
        jnc     .played
        mov     si, s_punsup
        call    log_line
        jmp     .flush
.played:
        mov     si, s_pfired
        cmp     byte [irqflag], 0
        jne     .pl
        mov     al, 3
        call    setrc
        mov     si, s_ptimeout
.pl:    call    log_line
.flush: mov     dx, f_sb
        jmp     log_flush

mode_sb:
        jmp     sb_test

; ---------------------------------------------------------------- SAVE

checksum:                       ; 16-bit sum of the first 12 bytes of rec -> AX
        mov     si, rec
        mov     cx, 12
        xor     ax, ax
        xor     bh, bh
.l:     mov     bl, [si]
        add     ax, bx
        inc     si
        loop    .l
        ret

; Load the save file named at DX into rec / cnt.
; AL = 0 valid, 1 cannot open (treated as absent), 2 corrupt.
load_save:
        mov     ax, 3D00h
        int     21h
        jnc     .op
        mov     al, 1
        ret
.op:    mov     bx, ax
        mov     ah, 3Fh
        mov     cx, SAVE_LEN+1  ; one extra byte detects an over-long file
        mov     dx, rec
        int     21h
        pushf
        push    ax
        mov     ah, 3Eh
        int     21h
        pop     ax
        popf
        jc      .bad
        cmp     ax, SAVE_LEN
        jne     .bad
        mov     si, rec
        mov     di, s_magic
        mov     cx, 8
        repe    cmpsb
        jne     .bad
        call    checksum
        cmp     ax, [rec+12]
        jne     .bad
        mov     ax, [rec+8]
        mov     [cnt], ax
        mov     ax, [rec+10]
        mov     [cnt+2], ax
        xor     al, al
        ret
.bad:   mov     al, 2
        ret

log_cnt:                        ; SI = prefix
        call    log_str
        mov     ax, [cnt]
        mov     dx, [cnt+2]
        call    log_hex32
        jmp     log_crlf

mode_save:
        mov     si, n_save
        call    log_begin
        mov     dx, f_dat
        call    load_save
        cmp     al, 0
        je      .dat
        cmp     al, 2
        je      .corrupt
        ; SAVE.DAT absent: a valid SAVE.TMP means a previous run stopped
        ; between delete and rename, and that TMP holds the newest counter.
        mov     dx, f_tmp
        call    load_save
        cmp     al, 0
        je      .tmp
        mov     si, s_srcnone
        call    log_line
        mov     si, s_oldnone
        call    log_line
        mov     word [cnt], 0
        mov     word [cnt+2], 0
        jmp     .inc
.corrupt:
        mov     si, s_srcdat
        call    log_line
        mov     si, s_oldbad
        call    log_line
        mov     si, s_unchanged
        call    log_line
        mov     al, 4
        call    setrc
        jmp     .flush
.tmp:   mov     si, s_srctmp
        jmp     .old
.dat:   mov     si, s_srcdat
.old:   call    log_line
        mov     si, s_old
        call    log_cnt
.inc:
        add     word [cnt], 1
        adc     word [cnt+2], 0
        mov     si, s_new
        call    log_cnt
        mov     si, s_magic
        mov     di, rec
        mov     cx, 8
        rep     movsb
        mov     ax, [cnt]
        mov     [rec+8], ax
        mov     ax, [cnt+2]
        mov     [rec+10], ax
        call    checksum
        mov     [rec+12], ax

        mov     ah, 3Ch
        xor     cx, cx
        mov     dx, f_tmp
        int     21h
        jc      .wfail
        mov     bx, ax
        mov     ah, 40h
        mov     cx, SAVE_LEN
        mov     dx, rec
        int     21h
        pushf
        push    ax
        mov     ah, 3Eh
        int     21h
        jc      .wfail2
        pop     ax
        popf
        jc      .wfail
        cmp     ax, SAVE_LEN
        jne     .wfail
        ; DOS rename (AH=56h) fails when the target exists, so the old file
        ; is deleted first. A stop between the two calls leaves only a valid
        ; SAVE.TMP, which the next run picks up (SOURCE SAVE.TMP).
        mov     ah, 41h
        mov     dx, f_dat
        int     21h             ; absence is fine
        mov     ah, 56h
        mov     dx, f_tmp
        mov     di, f_dat
        int     21h
        jc      .rfail
        mov     si, s_resok
        call    log_line
        jmp     .flush
.wfail2:
        pop     ax
        popf
.wfail: mov     si, s_reswf
        jmp     .fail
.rfail: mov     si, s_resrf
.fail:  call    log_line
        mov     al, 2
        call    setrc
.flush: mov     dx, f_savelog
        jmp     log_flush

; ---------------------------------------------------------------- ALL

mode_all:
        call    sb_test
        jmp     mode_save

; ---------------------------------------------------------------- TIME

; One line per 18 BIOS ticks (about 1 s of emulated time): the tick count,
; the CMOS clock (host time in DOSBox) and how many times a fixed loop ran
; in that window. A host pause shows as an RTC jump with ticks unchanged;
; a CPU speed change shows as a change in the loop count.
mode_time:
        mov     si, n_time
        call    log_begin
        mov     dx, m_time
        call    print_dollar
        mov     word [notes], 0
        call    get_ticks
        mov     [t0], ax
.edge:  call    get_ticks               ; start on a tick boundary
        cmp     ax, [t0]
        je      .edge
.window:
        mov     [t0], ax
        mov     [t0+2], dx
        mov     word [cnt], 0
        mov     word [cnt+2], 0
.spin:  add     word [cnt], 1
        adc     word [cnt+2], 0
        call    get_ticks
        sub     ax, [t0]
        cmp     ax, TIME_WINDOW
        jb      .spin
        call    get_ticks
        mov     [t1], ax
        mov     [t1+2], dx
        mov     al, 'T'
        call    log_ch
        call    log_space
        mov     ax, [t1]
        mov     dx, [t1+2]
        call    log_hex32
        call    log_space
        mov     ah, 02h
        int     1Ah
        mov     al, ch
        call    log_hex8
        mov     al, cl
        call    log_hex8
        mov     al, dh
        call    log_hex8
        call    log_space
        mov     ax, [cnt]
        mov     dx, [cnt+2]
        call    log_hex32
        call    log_crlf
        call    log_nearly_full
        jc      .full
        inc     word [notes]
        cmp     word [notes], TIME_LINES
        jae     .timeout
        mov     ah, 11h
        int     16h
        jz      .nokey
        mov     ah, 10h
        int     16h
        cmp     ax, 011Bh
        je      .esc
.nokey: mov     ax, [t1]
        jmp     .window
.esc:   mov     si, s_end_esc
        call    log_line
        jmp     .out
.timeout:
        mov     si, s_end_timeout
        call    log_line
        mov     al, 5
        call    setrc
        jmp     .out
.full:  mov     si, s_end_full
        call    log_line
        mov     al, 6
        call    setrc
.out:   mov     dx, f_time
        jmp     log_flush

; ---------------------------------------------------------------- GFX

; Text mode -> VGA 320x200x256 test card -> text mode. The card has 16
; colour bars, a white border and a white outlined 120x100 box, which a
; 4:3 display shows as a square. Nothing is printed while in mode 13h; the
; modes reported by INT 10h AH=0Fh are logged after the return.
mode_gfx:
        mov     si, n_gfx
        call    log_begin
        mov     ah, 0Fh
        int     10h
        mov     [lastx], al
        mov     ax, 0013h
        int     10h
        mov     ah, 0Fh
        int     10h
        mov     [lasty], al
        push    es
        mov     ax, 0A000h
        mov     es, ax
        xor     di, di
        xor     dx, dx                  ; y
.row:   xor     cx, cx                  ; x
.col:   mov     ax, cx
        mov     bl, 20
        div     bl                      ; AL = x / 20 = bar colour
        cmp     dx, 0
        je      .white
        cmp     dx, 199
        je      .white
        cmp     cx, 0
        je      .white
        cmp     cx, 319
        je      .white
        cmp     dx, 50                  ; box outline x 100..219, y 50..149
        jb      .put
        cmp     dx, 149
        ja      .put
        cmp     cx, 100
        jb      .put
        cmp     cx, 219
        ja      .put
        cmp     dx, 50
        je      .white
        cmp     dx, 149
        je      .white
        cmp     cx, 100
        je      .white
        cmp     cx, 219
        je      .white
        mov     al, 0
        jmp     .put
.white: mov     al, 15
.put:   stosb
        inc     cx
        cmp     cx, 320
        jb      .col
        inc     dx
        cmp     dx, 200
        jb      .row
        pop     es
        mov     word [gfxfile], f_gfx
gfx_wait:
        call    get_ticks
        mov     [idle], ax
.wait:  mov     ah, 11h
        int     16h
        jnz     .key
        call    get_ticks
        sub     ax, [idle]
        cmp     ax, IDLE_TICKS
        jb      .wait
        mov     byte [lastb], 0
        jmp     .back
.key:   mov     ah, 10h
        int     16h
        mov     byte [lastb], 1
.back:  mov     ax, 0003h
        int     10h
        mov     ah, 0Fh
        int     10h
        mov     [lastb+1], al
        mov     si, s_mode
        call    log_str
        mov     al, [lastx]
        call    log_hex8
        call    log_crlf
        mov     si, s_mode
        call    log_str
        mov     al, [lasty]
        call    log_hex8
        call    log_crlf
        mov     si, s_gkey
        cmp     byte [lastb], 1
        je      .k
        mov     si, s_gtimeout
        mov     al, 5
        call    setrc
.k:     call    log_line
        mov     si, s_mode
        call    log_str
        mov     al, [lastb+1]
        call    log_hex8
        call    log_crlf
        mov     si, s_end
        call    log_line
        mov     dx, [gfxfile]
        jmp     log_flush

; GFX12 (added 2026-10-02, stage 2c): text -> VGA 640x480x16 (mode 12h) ->
; text, same log format as GFX in BOXGFX12.LOG. Mode 12h is the one VGA mode
; whose frame differs in size from 80x25 text (640x400) in DOSBox, so the
; front end must reallocate its frame buffer on both switches. Only a white
; top and bottom row are drawn (INT 10h AH=0Ch).
mode_gfx12:
        mov     si, n_gfx12
        call    log_begin
        mov     ah, 0Fh
        int     10h
        mov     [lastx], al
        mov     ax, 0012h
        int     10h
        mov     ah, 0Fh
        int     10h
        mov     [lasty], al
        xor     cx, cx
.row:   mov     ax, 0C0Fh
        xor     bh, bh
        xor     dx, dx
        int     10h
        mov     ax, 0C0Fh
        mov     dx, 479
        int     10h
        inc     cx
        cmp     cx, 640
        jb      .row
        mov     word [gfxfile], f_gfx12
        jmp     gfx_wait

; ---------------------------------------------------------------- data

modetab:
        dw      n_keys,  mode_keys
        dw      n_mouse, mode_mouse
        dw      n_adlib, mode_adlib
        dw      n_sb,    mode_sb
        dw      n_save,  mode_save
        dw      n_all,   mode_all
        dw      n_time,  mode_time
        dw      n_gfx,   mode_gfx
        dw      n_gfx12, mode_gfx12
        dw      0

n_keys          db "KEYS", 0
n_mouse         db "MOUSE", 0
n_adlib         db "ADLIB", 0
n_sb            db "SB", 0
n_save          db "SAVE", 0
n_all           db "ALL", 0
n_time          db "TIME", 0
n_gfx           db "GFX", 0
n_gfx12         db "GFX12", 0

f_keys          db "BOXKEYS.LOG", 0
f_mouse         db "BOXMOUSE.LOG", 0
f_adlib         db "BOXADLIB.LOG", 0
f_sb            db "BOXSB.LOG", 0
f_savelog       db "BOXSAVE.LOG", 0
f_dat           db "SAVE.DAT", 0
f_tmp           db "SAVE.TMP", 0
f_time          db "BOXTIME.LOG", 0
f_gfx           db "BOXGFX.LOG", 0
f_gfx12         db "BOXGFX12.LOG", 0

s_header        db "BOXTEST 1.0 ", 0
s_end_esc       db "END ESC", 0
s_end_right     db "END RIGHT", 0
s_end_timeout   db "END TIMEOUT", 0
s_end_full      db "END FULL", 0
s_mpresent      db "DRIVER PRESENT BUTTONS ", 0
s_mabsent       db "DRIVER ABSENT", 0
s_apresent      db "DETECT PRESENT", 0
s_aabsent       db "DETECT ABSENT", 0
s_start         db "START TICK ", 0
s_endtick       db "END TICK ", 0
s_elapsed       db "ELAPSED TICKS ", 0
s_notes         db "NOTES ", 0
s_blaster       db "BLASTER=", 0
s_cfg_a         db "CONFIG A=", 0
s_cfg_i         db " I=", 0
s_cfg_d         db " D=", 0
s_src_def       db " SOURCE=DEFAULT", 0
s_src_env       db " SOURCE=ENV", 0
s_rok           db "DSP RESET OK", 0
s_rfail         db "DSP RESET FAIL", 0
s_ver           db "DSP VERSION ", 0
s_vfail         db "DSP VERSION FAIL", 0
s_pfired        db "PLAYBACK IRQ FIRED", 0
s_ptimeout      db "PLAYBACK IRQ TIMEOUT", 0
s_pskip         db "PLAYBACK SKIPPED", 0
s_punsup        db "PLAYBACK UNSUPPORTED", 0
s_srcnone       db "SOURCE NONE", 0
s_srcdat        db "SOURCE SAVE.DAT", 0
s_srctmp        db "SOURCE SAVE.TMP", 0
s_oldnone       db "OLD NONE", 0
s_oldbad        db "OLD CORRUPT", 0
s_old           db "OLD ", 0
s_new           db "NEW ", 0
s_unchanged     db "RESULT UNCHANGED", 0
s_resok         db "RESULT OK", 0
s_reswf         db "RESULT WRITE FAIL", 0
s_resrf         db "RESULT RENAME FAIL", 0
s_magic         db "BOXSAVE1"
s_mode          db "MODE ", 0
s_gkey          db "WAIT KEY", 0
s_gtimeout      db "WAIT TIMEOUT", 0
s_end           db "END", 0

m_keys          db "Press keys; ESC ends.", 13, 10, "$"
m_mouse         db "Move/click; right button or ESC ends.", 13, 10, "$"
m_time          db "Timing; ESC ends.", 13, 10, "$"
m_ioerr         db "BOXTEST: log write failed", 13, 10, "$"
usage_msg       db "BOXTEST 1.0 - Boxer port test fixture", 13, 10
                db "Usage: BOXTEST KEYS|MOUSE|ADLIB|SB|SAVE|ALL|TIME|GFX|GFX12", 13, 10
                db "Errorlevel: 0 ok, 1 usage, 2 file I/O, 3 hardware,", 13, 10
                db "  4 SAVE.DAT corrupt, 5 idle timeout, 6 log full", 13, 10, "$"

; OPL2 channel 0, operators 0 (modulator) and 3 (carrier): a plain organ-like
; tone. Pairs are (register << 8 | value), zero-terminated.
adlib_init:
        dw      0120h           ; enable waveform select (harmless, deterministic)
        dw      2001h, 2301h    ; multiplier 1
        dw      4010h, 4300h    ; modulator level, carrier full volume
        dw      60F0h, 63F0h    ; fast attack, medium decay
        dw      8077h, 8377h    ; sustain/release
        dw      0E000h, 0E300h  ; sine waveforms
        dw      0C000h          ; FM, feedback 0
        dw      0

; F-numbers for block 5 (f = fnum * 49716 / 2^15): C4 D4 E4 F4 G4 A4 B4 C5.
fnums           dw 345, 387, 435, 460, 517, 580, 651, 690

; 8237 page registers for channels 0..3.
dmapage         db 87h, 83h, 81h, 82h

sb_base         dw 220h
sb_irq          db 7
sb_dma          db 1
sb_src          db 0
exitcode        db 0

        section .bss
argbuf          resb 9
logpos          resw 1
linestart       resw 1
idle            resw 1
lastk           resw 1
lastx           resw 1
lasty           resw 1
lastb           resw 1
gfxfile         resw 1
curnote         resb 1
notes           resw 1
t0              resw 2
t1              resw 2
sb_vec          resb 1
oldvec          resw 2
picsave         resb 1
irqflag         resb 1
dmaoff          resw 1
pagebyte        resb 1
cnt             resw 2
rec             resb SAVE_LEN+1
logbuf          resb LOGMAX
dmabuf          resb SB_LEN*2
