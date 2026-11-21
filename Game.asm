;Semester Catcher ,game logic + file handling (MASM, x86)
;GUI (Win32 GDI,C++) calls these procedures via extern "C".
.386
.model flat, C

INCLUDELIB C:\Irvine\Irvine32.lib
INCLUDELIB kernel32.lib
INCLUDELIB user32.lib

;Irvine32 file procedures (register based, stdcall)
CreateOutputFile PROTO STDCALL   ;edx = filename -> eax = handle
OpenInputFile    PROTO STDCALL   ;edx = filename -> eax = handle (-1 if missing)
ReadFromFile     PROTO STDCALL   ;eax = handle, edx = buffer, ecx = bytes -> eax = bytes read
WriteToFile      PROTO STDCALL   ;eax = handle, edx = buffer, ecx = bytes -> eax = bytes written
CloseFile        PROTO STDCALL   ;eax = handle

;our own prototypes (so INVOKE works in any order)
RandRange   PROTO C, n:DWORD
PutDigits   PROTO C, n:DWORD, buf:PTR BYTE
ScoreToStr  PROTO C, n:DWORD, buf:PTR BYTE
WriteScores PROTO C

;constants must match main.cpp
WIN_W       EQU 800
WIN_H       EQU 600
BASKET_Y    EQU 540
BASKET_W    EQU 120
BASKET_H    EQU 20
BASKET_SPD  EQU 10
ITEM_W      EQU 100
ITEM_H      EQU 32
MAX_ITEMS   EQU 30
LANES       EQU 5                           ;items fall only in the 5 middle columns
LANE_X0     EQU (WIN_W - LANES * ITEM_W) / 2  ;x of the first lane (= 150)

MAX_ENT     EQU 50          ;max leaderboard entries
NAME_LEN    EQU 16          ;15 chars + null
LINE_LEN    EQU 36          ;bytes per file line (incl. CR LF)
FILE_MAX    EQU 2048

PUBLIC gScore, gLives, gLevel, gBasketX
PUBLIC itemX, itemY, itemType, itemActive
PUBLIC gCount, bScore, bLevel, bName

.data
;game state (read by the GUI)
gScore       SDWORD 0
gLives       SDWORD 3
gLevel       SDWORD 1
gBasketX     SDWORD 340

;internal state
gSpeed       SDWORD 5
gMult        SDWORD 2
gBadPct      SDWORD 35
gSpawnDelay  SDWORD 35
gSpawnTimer  SDWORD 1
gSeed        DWORD  1

;item arrays: x, y, type, active
itemX        SDWORD MAX_ITEMS DUP(0)
itemY        SDWORD MAX_ITEMS DUP(0)
itemType     SDWORD MAX_ITEMS DUP(0)
itemActive   SDWORD MAX_ITEMS DUP(0)

;difficulty tables: Easy,Medium,Hard
levelSpeed   SDWORD 3, 5, 7
levelSpawn   SDWORD 50, 35, 22
levelLives   SDWORD 3, 3, 2
levelBad     SDWORD 25, 35, 45
levelMult    SDWORD 1, 2, 3

;points per good item: Assignment,Quiz,Exam
points       SDWORD 10, 20, 50

;strings
nm0 BYTE "Assignment", 0
nm1 BYTE "Quiz", 0
nm2 BYTE "Exam", 0
nm3 BYTE "Bunk Class", 0
nm4 BYTE "Proxy", 0
nameTable DWORD OFFSET nm0, OFFSET nm1, OFFSET nm2, OFFSET nm3, OFFSET nm4

lv0 BYTE "Easy", 0
lv1 BYTE "Medium", 0
lv2 BYTE "Hard", 0
levelTable DWORD OFFSET lv0, OFFSET lv1, OFFSET lv2

;leaderboard(sorted high -> low)
gCount       SDWORD 0
bScore       SDWORD MAX_ENT DUP(0)
bLevel       SDWORD MAX_ENT DUP(0)
bName        BYTE   MAX_ENT * NAME_LEN DUP(0)

;file handling
fileName     BYTE "highscores.txt", 0
hdrLine      BYTE "RK NAME", 11 DUP(' '), ' ', "   SCORE", ' ', "LEVEL ", 13, 10   ; = 36 bytes
fileBuf      BYTE FILE_MAX DUP(0)
numBuf       BYTE 16 DUP(0)
bytesIO      DWORD 0
hFile        DWORD 0
totalBytes   DWORD 0

.code

; RandRange(n) -> eax in [0, n-1]   (LCG: seed*1103515245+12345)
RandRange PROC C, n:DWORD
    mov eax, gSeed
    mov ecx, 1103515245
    mul ecx                     ; edx:eax = seed * a
    add eax, 12345
    mov gSeed, eax
    shr eax, 16                 ; use the higher, better bits
    xor edx, edx
    div n                       ; edx = eax mod n
    mov eax, edx
    ret
RandRange ENDP

; PutDigits(n, buf) - RECURSIVE: writes decimal digits of n,
; returns pointer just past the last digit written in eax.

PutDigits PROC C USES ebx edx, n:DWORD, buf:PTR BYTE
    mov eax, n
    xor edx, edx
    mov ebx, 10
    div ebx                         ; eax = n/10, edx = n%10
    push edx                        ; keep this digit
    cmp eax, 0
    je digits_base
    INVOKE PutDigits, eax, buf      ; recurse on the higher digits
    jmp digits_emit
digits_base:
    mov eax, buf
digits_emit:
    pop edx
    add dl, '0'
    mov BYTE PTR [eax], dl
    inc eax
    ret
PutDigits ENDP

; ScoreToStr(n, buf) - n -> null-terminated decimal string

ScoreToStr PROC C, n:DWORD, buf:PTR BYTE
    INVOKE PutDigits, n, buf
    mov BYTE PTR [eax], 0
    ret
ScoreToStr ENDP


; GameInit(level, seed) - reset everything using difficulty tables

GameInit PROC C USES ebx, level:DWORD, seed:DWORD
    mov ebx, level
    mov gLevel, ebx

    mov eax, levelSpeed[ebx*4]
    mov gSpeed, eax
    mov eax, levelSpawn[ebx*4]
    mov gSpawnDelay, eax
    mov eax, levelLives[ebx*4]
    mov gLives, eax
    mov eax, levelBad[ebx*4]
    mov gBadPct, eax
    mov eax, levelMult[ebx*4]
    mov gMult, eax

    mov eax, seed
    mov gSeed, eax
    mov gScore, 0
    mov gBasketX,(WIN_W - BASKET_W) / 2
    mov gSpawnTimer, 1

    mov ecx, MAX_ITEMS
clear_loop:
    mov itemActive[ecx*4 - 4], 0
    loop clear_loop
    ret
GameInit ENDP

; SpawnItem - put a new good/bad item in the first free slot

SpawnItem PROC C USES esi
    xor esi, esi
find_slot:
    cmp esi, MAX_ITEMS
    jae spawn_done
    cmp itemActive[esi*4], 0
    je slot_found
    inc esi
    jmp find_slot

slot_found:
    mov itemActive[esi*4], 1
    mov itemY[esi*4], -ITEM_H

    INVOKE RandRange, LANES         ;pick one of the 5 middle lanes (0..4)
    imul eax, eax, ITEM_W
    add eax, LANE_X0
    mov itemX[esi*4], eax

    INVOKE RandRange, 100
    cmp eax, gBadPct
    jl make_bad
    INVOKE RandRange, 3             ; good: 0..2
    mov itemType[esi*4], eax
    jmp spawn_done
make_bad:
    INVOKE RandRange, 2             ; bad: 3..4
    add eax, 3
    mov itemType[esi*4], eax
spawn_done:
    ret
SpawnItem ENDP

; GameUpdate(dir) - one frame: move basket, spawn, move items,
; collisions, score/lives.  dir = -1, 0, +1.  Returns lives left.

GameUpdate PROC C USES ebx esi, dir:SDWORD
    ;basket movement + clamping
    mov ecx, dir
    imul ecx, ecx, BASKET_SPD
    mov eax, gBasketX
    add eax, ecx
    cmp eax, 0
    jge left_ok
    xor eax, eax
left_ok:
    cmp eax, WIN_W - BASKET_W
    jle right_ok
    mov eax, WIN_W - BASKET_W
right_ok:
    mov gBasketX, eax

    ;spawn timer
    dec gSpawnTimer
    jg no_spawn
    mov eax, gSpawnDelay
    mov gSpawnTimer, eax
    call SpawnItem
no_spawn:

    ;move items + collision
    xor esi, esi
item_loop:
    cmp esi, MAX_ITEMS
    jae loop_done
    cmp itemActive[esi*4], 0
    je next_item

    mov eax, itemY[esi*4]
    add eax, gSpeed
    mov itemY[esi*4], eax

    cmp eax, WIN_H                  ; fell off screen
    jl check_hit
    mov itemActive[esi*4], 0
    cmp itemType[esi*4], 3
    jae next_item                   ; a missed BAD item is fine
    dec gLives                      ; a missed GOOD item costs a life
    jmp next_item

check_hit:
    mov ebx, eax
    add ebx, ITEM_H                 ; item bottom
    cmp ebx, BASKET_Y
    jl next_item
    cmp eax, BASKET_Y + BASKET_H    ; item top
    jg next_item

    mov ebx, itemX[esi*4]
    add ebx, ITEM_W                 ; item right edge
    cmp ebx, gBasketX
    jle next_item

    mov ebx, itemX[esi*4]
    mov edx, gBasketX
    add edx, BASKET_W               ; basket right edge
    cmp ebx, edx
    jge next_item

    ;caught
    mov itemActive[esi*4], 0
    mov ebx, itemType[esi*4]
    cmp ebx, 3
    jae caught_bad
    mov eax, points[ebx*4]
    imul eax, gMult                 ; score multiplier by difficulty
    add gScore, eax
    jmp next_item
caught_bad:
    dec gLives

next_item:
    inc esi
    jmp item_loop

loop_done:
    cmp gLives, 0
    jge lives_ok
    mov gLives, 0
lives_ok:
    mov eax, gLives
    ret
GameUpdate ENDP

; String lookups (pointer tables via OFFSET)
GetItemName PROC C, t:DWORD
    mov eax, t
    mov eax, nameTable[eax*4]
    ret
GetItemName ENDP

GetLevelName PROC C, lv:DWORD
    mov eax, lv
    mov eax, levelTable[eax*4]
    ret
GetLevelName ENDP

;                    FILE HANDLING / LEADERBOARD
; LoadScores() - read highscores.txt into the bName/bScore/bLevel
; arrays. If the file does not exist, the board is simply empty.

LoadScores PROC C USES ebx esi edi
    mov gCount, 0
    mov edx, OFFSET fileName
    call OpenInputFile              ; Irvine32: eax = file handle
    cmp eax, -1                     ; INVALID_HANDLE_VALUE -> no file yet
    je load_done
    mov hFile, eax
    mov edx, OFFSET fileBuf
    mov ecx, FILE_MAX
    call ReadFromFile               ; eax = handle in, bytes read out
    jnc read_ok
    xor eax, eax                    ; carry set = read error -> treat as empty
read_ok:
    mov bytesIO, eax
    mov eax, hFile
    call CloseFile

    ; number of data lines = (bytes / LINE_LEN) - 1 header line
    mov eax, bytesIO
    xor edx, edx
    mov ecx, LINE_LEN
    div ecx
    dec eax
    jle load_done
    cmp eax, MAX_ENT
    jbe cap_ok
    mov eax, MAX_ENT
cap_ok:
    mov edi, eax                    ; edi = entries to parse
    xor esi, esi                    ; esi = entry index

parse_loop:
    cmp esi, edi
    jae parse_done

    ; ebx -> start of this line in fileBuf (skip the header line)
    mov eax, esi
    inc eax
    imul eax, eax, LINE_LEN
    lea ebx, fileBuf[eax]

    ;name: columns 3..17, trailing spaces trimmed
    mov eax, esi
    shl eax, 4                      ; * NAME_LEN (16)
    lea edx, bName[eax]             ; edx = destination
    xor ecx, ecx
copy_name:
    mov al, BYTE PTR [ebx + ecx + 3]
    mov BYTE PTR [edx + ecx], al
    inc ecx
    cmp ecx, 15
    jb copy_name
    mov BYTE PTR [edx + 15], 0
    mov ecx, 14
trim_name:
    cmp BYTE PTR [edx + ecx], ' '
    jne trim_done
    mov BYTE PTR [edx + ecx], 0
    dec ecx
    jns trim_name
trim_done:

    ;score: columns 19..26 (right aligned, spaces skipped)
    xor eax, eax
    xor ecx, ecx
score_loop:
    movzx edx, BYTE PTR [ebx + 19 + ecx]
    sub edx, '0'
    cmp edx, 9
    ja score_skip                   ; not a digit (space)
    imul eax, eax, 10
    add eax, edx
score_skip:
    inc ecx
    cmp ecx, 8
    jb score_loop
    mov bScore[esi*4], eax

    ;level: first letter at column 28 (E / M / H)
    movzx eax, BYTE PTR [ebx + 28]
    xor edx, edx                    ; 0 = Easy
    cmp al, 'M'
    jne not_med
    mov edx, 1
not_med:
    cmp al, 'H'
    jne not_hard
    mov edx, 2
not_hard:
    mov bLevel[esi*4], edx

    inc esi
    jmp parse_loop

parse_done:
    mov gCount, edi
load_done:
    ret
LoadScores ENDP

; WriteScores() - format the whole board as text and write it to
; highscores.txt (header line + one fixed-width line per entry).
WriteScores PROC C USES ebx esi edi
    cld
    ;header line (string instruction: rep movsb)
    mov esi, OFFSET hdrLine
    mov edi, OFFSET fileBuf
    mov ecx, LINE_LEN
    rep movsb                       ; edi now = start of first data line

    xor ebx, ebx                    ; ebx = entry index
w_loop:
    cmp ebx, gCount
    jge w_write

    ;blank the line with spaces + CR LF (rep stosb)
    push edi
    mov al, ' '
    mov ecx, LINE_LEN - 2
    rep stosb
    mov BYTE PTR [edi], 13
    mov BYTE PTR [edi + 1], 10
    pop edi                         ; edi = line start again

    ;rank: two digits (div by 10)
    mov eax, ebx
    inc eax
    mov ecx, 10
    xor edx, edx
    div ecx                         ; eax = tens, edx = units
    add al, '0'
    mov BYTE PTR [edi], al
    add dl, '0'
    mov BYTE PTR [edi + 1], dl

    ;name: columns 3..17
    mov esi, ebx
    shl esi, 4
    add esi, OFFSET bName
    xor ecx, ecx
w_name:
    cmp ecx, 15
    jae w_name_done
    mov al, BYTE PTR [esi + ecx]
    cmp al, 0
    je w_name_done
    mov BYTE PTR [edi + ecx + 3], al
    inc ecx
    jmp w_name
w_name_done:

    ;score: right aligned in columns 19..26
    INVOKE ScoreToStr, bScore[ebx*4], OFFSET numBuf
    xor ecx, ecx
w_len:
    cmp BYTE PTR numBuf[ecx], 0
    je w_len_done
    inc ecx
    jmp w_len
w_len_done:                         ; ecx = number of digits
    mov edx, 8
    sub edx, ecx
    lea edx, [edi + 19 + edx]       ; destination inside the line
    push edi
    mov edi, edx
    mov esi, OFFSET numBuf
    rep movsb                       ; copy digits
    pop edi

    ;difficulty text: columns 28..33
    mov eax, bLevel[ebx*4]
    mov esi, levelTable[eax*4]
    xor ecx, ecx
w_lv:
    mov al, BYTE PTR [esi + ecx]
    cmp al, 0
    je w_lv_done
    mov BYTE PTR [edi + ecx + 28], al
    inc ecx
    jmp w_lv
w_lv_done:

    add edi, LINE_LEN               ; next line
    inc ebx
    jmp w_loop

w_write:
    mov eax, gCount
    inc eax
    imul eax, eax, LINE_LEN
    mov totalBytes, eax             ; total bytes to write
    mov edx, OFFSET fileName
    call CreateOutputFile           ; Irvine32: eax = file handle
    cmp eax, -1
    je w_done
    mov hFile, eax
    mov edx, OFFSET fileBuf
    mov ecx, totalBytes
    call WriteToFile                ; eax = handle in
    mov eax, hFile
    call CloseFile
w_done:
    ret
WriteScores ENDP

; QualifiesForBoard(score) -> 1 if this score belongs on the board
QualifiesForBoard PROC C, score:SDWORD
    xor eax, eax
    cmp score, 0
    jle q_done                      ; zero / negative never qualifies
    cmp gCount, MAX_ENT
    jl q_yes                        ; board not full yet
    mov edx, score
    cmp edx, bScore[MAX_ENT*4 - 4]  ; must beat the last place
    jle q_done
q_yes:
    mov eax, 1
q_done:
    ret
QualifiesForBoard ENDP

; SaveScore(name, score, level) -> rank (1..N), or 0 if not saved.
; Inserts into the sorted arrays (shifting lower entries down),
; then rewrites highscores.txt.

SaveScore PROC C USES ebx esi edi, nameP:PTR BYTE, score:SDWORD, lvl:SDWORD
    ;find insert position: first entry with a lower score
    xor ebx, ebx
find_pos:
    cmp ebx, gCount
    jge pos_found
    mov eax, score
    cmp eax, bScore[ebx*4]
    jg pos_found
    inc ebx
    jmp find_pos
pos_found:
    cmp ebx, MAX_ENT
    jge not_saved                   ; does not make the board

    ;new count (capped)
    mov eax, gCount
    inc eax
    cmp eax, MAX_ENT
    jle count_ok
    mov eax, MAX_ENT
count_ok:
    mov gCount, eax
    mov edi, eax
    dec edi                         ; edi = last index

    ;shift entries [pos .. last-1] one place down
shift_loop:
    cmp edi, ebx
    jle shift_done
    mov eax, bScore[edi*4 - 4]
    mov bScore[edi*4], eax
    mov eax, bLevel[edi*4 - 4]
    mov bLevel[edi*4], eax
    mov esi, edi
    shl esi, 4                      ; destination name offset
    xor ecx, ecx
shift_name:
    mov al, BYTE PTR bName[esi + ecx - 16]   ; source = previous entry's name
    mov BYTE PTR bName[esi + ecx], al
    inc ecx
    cmp ecx, NAME_LEN
    jb shift_name
    dec edi
    jmp shift_loop

shift_done:
    ;put the new entry at position ebx
    mov eax, score
    mov bScore[ebx*4], eax
    mov eax, lvl
    mov bLevel[ebx*4], eax
    mov esi, ebx
    shl esi, 4
    mov edi, nameP
    xor ecx, ecx
ins_name:
    cmp ecx, 15
    jae ins_done
    mov al, BYTE PTR [edi + ecx]
    cmp al, 0
    je ins_done
    mov BYTE PTR bName[esi + ecx], al
    inc ecx
    jmp ins_name
ins_done:
    mov bName[esi + ecx], 0

    call WriteScores
    mov eax, ebx
    inc eax                         ; rank = index + 1
    jmp save_exit
not_saved:
    xor eax, eax
save_exit:
    ret
SaveScore ENDP

; ClearScores() - wipe the board and rewrite the file (header only)

ClearScores PROC C
    mov gCount, 0
    call WriteScores
    ret
ClearScores ENDP

END