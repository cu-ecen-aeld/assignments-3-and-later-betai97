# Analysis of faulty oops

## Command Outputs

- Below shows output of the OOPS when writing to faulty device

```
echo “hello_world” > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000042117000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 158 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008c83d80
x29: ffffffc008c83d80 x28: ffffff80024d2640 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 0000000000000012 x21: 000000558abf2aa0
x20: 000000558abf2aa0 x19: ffffff800209f100 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008c83df0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0aa78caa46daee73 ]---
```

- Below shows output of objdump run on our .ko module

```
taitbenj@Ubunru:~/assignment-4-betai97/buildroot$ ./output/host/bin/aarch64-linux-objdump -S ./output/target/lib/modules/5.15.18/extra/faulty.ko

./output/target/lib/modules/5.15.18/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0: d503245f  bti c
   4: d2800001  mov x1, #0x0                    // #0
   8: d2800000  mov x0, #0x0                    // #0
   c: d503233f  paciasp
  10: d50323bf  autiasp
  14: b900003f  str wzr, [x1]
  18: d65f03c0  ret
  1c: d503201f  nop

0000000000000020 <faulty_read>:
  20: d503233f  paciasp
  24: a9bd7bfd  stp x29, x30, [sp, #-48]!
  28: d5384100  mrs x0, sp_el0
  2c: 910003fd  mov x29, sp
  30: f9000bf3  str x19, [sp, #16]
  34: 92800005  mov x5, #0xffffffffffffffff     // #-1
  38: 12800003  mov w3, #0xffffffff             // #-1
  3c: f100105f  cmp x2, #0x4
  40: f941f804  ldr x4, [x0, #1008]
  44: f90017e4  str x4, [sp, #40]
  48: d2800004  mov x4, #0x0                    // #0
  4c: 910013e0  add x0, sp, #0x4
  50: 92800004  mov x4, #0xffffffffffffffff     // #-1
  54: d2800093  mov x19, #0x4                     // #4
  58: 9a939053  csel  x19, x2, x19, ls  // ls = plast
  5c: a9021404  stp x4, x5, [x0, #32]
  60: d5384100  mrs x0, sp_el0
  64: b90037e3  str w3, [sp, #52]
  68: b9402403  ldr w3, [x0, #36]
  6c: 37a80363  tbnz  w3, #21, d8 <faulty_read+0xb8>
  70: f9400000  ldr x0, [x0]
  74: aa0103e2  mov x2, x1
  78: 7206001f  tst w0, #0x4000000
  7c: 540002e1  b.ne  d8 <faulty_read+0xb8>  // b.any
  80: b2409be3  mov x3, #0x7fffffffff           // #549755813887
  84: aa0303e0  mov x0, x3
  88: ab130042  adds  x2, x2, x19
  8c: 9a8083e0  csel  x0, xzr, x0, hi  // hi = pmore
  90: da9f3042  csinv x2, x2, xzr, cc  // cc = lo, ul, last
  94: fa00005f  sbcs  xzr, x2, x0
  98: 9a9f87e2  cset  x2, ls  // ls = plast
  9c: aa1303e0  mov x0, x19
  a0: b5000222  cbnz  x2, e4 <faulty_read+0xc4>
  a4: 7100001f  cmp w0, #0x0
  a8: d5384101  mrs x1, sp_el0
  ac: 93407c00  sxtw  x0, w0
  b0: 9a931000  csel  x0, x0, x19, ne  // ne = any
  b4: f94017e2  ldr x2, [sp, #40]
  b8: f941f823  ldr x3, [x1, #1008]
  bc: eb030042  subs  x2, x2, x3
  c0: d2800003  mov x3, #0x0                    // #0
  c4: 54000221  b.ne  108 <faulty_read+0xe8>  // b.any
  c8: f9400bf3  ldr x19, [sp, #16]
  cc: a8c37bfd  ldp x29, x30, [sp], #48
  d0: d50323bf  autiasp
  d4: d65f03c0  ret
  d8: 9340dc22  sbfx  x2, x1, #0, #56
  dc: 8a020022  and x2, x1, x2
  e0: 17ffffe8  b 80 <faulty_read+0x60>
  e4: 9340dc22  sbfx  x2, x1, #0, #56
  e8: 8a020022  and x2, x1, x2
  ec: ea23005f  bics  xzr, x2, x3
  f0: 9a9f0020  csel  x0, x1, xzr, eq  // eq = none
  f4: d503229f  csdb
  f8: 910093e1  add x1, sp, #0x24
  fc: aa1303e2  mov x2, x19
 100: 94000000  bl  0 <__arch_copy_to_user>
 104: 17ffffe8  b a4 <faulty_read+0x84>
 108: 94000000  bl  0 <__stack_chk_fail>
 10c: d503201f  nop

0000000000000110 <faulty_init>:
 110: d503233f  paciasp
 114: a9be7bfd  stp x29, x30, [sp, #-32]!
 118: 90000004  adrp  x4, 0 <faulty_write>
 11c: 910003fd  mov x29, sp
 120: f9000bf3  str x19, [sp, #16]
 124: 90000013  adrp  x19, 0 <faulty_write>
 128: b9400260  ldr w0, [x19]
 12c: 90000003  adrp  x3, 0 <faulty_write>
 130: 91000084  add x4, x4, #0x0
 134: 91000063  add x3, x3, #0x0
 138: 52802002  mov w2, #0x100                  // #256
 13c: 52800001  mov w1, #0x0                    // #0
 140: 94000000  bl  0 <__register_chrdev>
 144: 37f800a0  tbnz  w0, #31, 158 <faulty_init+0x48>
 148: b9400261  ldr w1, [x19]
 14c: 350000e1  cbnz  w1, 168 <faulty_init+0x58>
 150: b9000260  str w0, [x19]
 154: 52800000  mov w0, #0x0                    // #0
 158: f9400bf3  ldr x19, [sp, #16]
 15c: a8c27bfd  ldp x29, x30, [sp], #32
 160: d50323bf  autiasp
 164: d65f03c0  ret
 168: 52800000  mov w0, #0x0                    // #0
 16c: f9400bf3  ldr x19, [sp, #16]
 170: a8c27bfd  ldp x29, x30, [sp], #32
 174: d50323bf  autiasp
 178: d65f03c0  ret
 17c: d503201f  nop

0000000000000180 <cleanup_module>:
 180: d503233f  paciasp
 184: 90000000  adrp  x0, 0 <faulty_write>
 188: a9bf7bfd  stp x29, x30, [sp, #-16]!
 18c: 52802002  mov w2, #0x100                  // #256
 190: 52800001  mov w1, #0x0                    // #0
 194: 910003fd  mov x29, sp
 198: b9400000  ldr w0, [x0]
 19c: 90000003  adrp  x3, 0 <faulty_write>
 1a0: 91000063  add x3, x3, #0x0
 1a4: 94000000  bl  0 <__unregister_chrdev>
 1a8: a8c17bfd  ldp x29, x30, [sp], #16
 1ac: d50323bf  autiasp
 1b0: d65f03c0  ret

Disassembly of section .plt:

0000000000000000 <.plt>:
  ...

Disassembly of section .text.ftrace_trampoline:

0000000000000000 <.text.ftrace_trampoline>:
  ...
```

## Discussion
- NULL pointer dereference is clearly called out at the top of the oops
- Line `pc : faulty_write+0x14/0x20 [faulty]` tells us that it's `faulty` who faulted, in invoking the `faulty_write` function
- That line also tells us it's faulty_write at 0x14 who faulted, looking at objdump output that's line `14: b900003f  str wzr, [x1]`
- Looks like it's trying to de-reference address in `x1`, but that register contains zero/NULL