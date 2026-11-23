/* SPDX-License-Identifier: GPL-3.0-only */
.syntax unified
.cpu arm926ej-s
.arm
.text
.balign 4

.global cfw_lrc_property
.type cfw_lrc_property,%function
cfw_lrc_property:
    push {r1-r3, lr}
    mov r0, r4
    bl cfw_lrc_get_text
    cmp r0, #0
    pop {r1-r3, lr}
    ldrne pc, =0x081ee858
    ldr r0, [r4, #28]
    cmp r0, #0
    ldr pc, =0x081eecb8
.ltorg

.global cfw_lrc_event_original
.type cfw_lrc_event_original,%function
cfw_lrc_event_original:
    push {r4-r8, lr}
    mov r4, r0
    ldr pc, =0x081efe7c
.ltorg
