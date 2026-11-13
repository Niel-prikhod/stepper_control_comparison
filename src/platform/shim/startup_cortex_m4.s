  .syntax unified
  .cpu cortex-m4
  .fpu softvfp
  .thumb

  .global Reset_Handler
  .global Default_Handler

  .section .isr_vector, "a", %progbits
  .type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler
  .rept 48
  .word Default_Handler
  .endr

  .section .text.Reset_Handler, "ax", %progbits
  .thumb_func
Reset_Handler:
  ldr r0, =_estack
  mov sp, r0
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
  movs r3, #0
  b LoopCopyDataInit
CopyDataInit:
  ldr r4, [r2, r3]
  str r4, [r0, r3]
  adds r3, r3, #4
LoopCopyDataInit:
  adds r4, r0, r3
  cmp r4, r1
  bcc CopyDataInit
  ldr r2, =_sbss
  ldr r4, =_ebss
  movs r3, #0
  b LoopFillZerobss
FillZerobss:
  str r3, [r2]
  adds r2, r2, #4
LoopFillZerobss:
  cmp r2, r4
  bcc FillZerobss
  bl SystemInit
  bl main
  b .

  .thumb_func
Default_Handler:
  b .
  .weak NMI_Handler
  .weak HardFault_Handler
  .weak MemManage_Handler
  .weak BusFault_Handler
  .weak UsageFault_Handler
  .weak SVC_Handler
  .weak DebugMon_Handler
  .weak PendSV_Handler
  .weak SysTick_Handler
  .set NMI_Handler, Default_Handler
  .set HardFault_Handler, Default_Handler
  .set MemManage_Handler, Default_Handler
  .set BusFault_Handler, Default_Handler
  .set UsageFault_Handler, Default_Handler
  .set SVC_Handler, Default_Handler
  .set DebugMon_Handler, Default_Handler
  .set PendSV_Handler, Default_Handler
  .set SysTick_Handler, Default_Handler

  .weak SystemInit
  .set SystemInit, Default_Handler
