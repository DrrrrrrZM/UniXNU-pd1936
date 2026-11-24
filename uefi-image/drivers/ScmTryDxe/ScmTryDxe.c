/** @file
  Sweep SVC 0x15 cmd 0x02 (QCOM_SCM_SVC_SMMU_PROGRAM) over argument counts and plausible
  argument vectors, and probe the memory-protection calls that looked reachable.

  Uses ArmSmcLib's ArmCallSmc(): an earlier hand-written `smc #0` block corrupted callee-saved
  registers on return, which made the next DEBUG call assert inside DebugLib.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define SCM_FNID(s, c)      ((((UINTN)(s) & 0xFF) << 8) | ((UINTN)(c) & 0xFF))
#define SCM_FNID_SIP(s, c)  ((UINTN)0x80000000 | ((UINTN)2 << 24) | SCM_FNID (s, c))

typedef struct {
  UINTN  NArgs;
  UINTN  A0;
  UINTN  A1;
  UINTN  A2;
} TRY_VEC;

STATIC
VOID
Smc (
  IN OUT ARM_SMC_ARGS  *Args
  )
{
  ArmCallSmc (Args);
}

EFI_STATUS
EFIAPI
ScmTryDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST TRY_VEC  Vecs[] = {
    { 0, 0,     0,     0     },
    { 1, 0,     0,     0     },
    { 1, 1,     0,     0     },
    { 1, 0x300, 0,     0     },
    { 2, 0,     0,     0     },
    { 2, 0x300, 0,     0     },
    { 2, 0,     0x300, 0     },
    { 3, 0,     0,     0     },
    { 3, 0x300, 0,     0     },
    { 3, 0,     0,     0x300 },
    { 4, 0,     0,     0     },
    { 6, 0,     0,     0     }
  };
  ARM_SMC_ARGS  A;
  UINTN         I;
  UINTN         Ret;

  DEBUG ((DEBUG_ERROR, "ScmTry3: sweeping SVC 0x15 cmd 0x02 (SMMU_PROGRAM)\n"));

  for (I = 0; I < ARRAY_SIZE (Vecs); ++I) {
    ZeroMem (&A, sizeof (A));
    A.Arg0 = SCM_FNID_SIP (0x15, 0x02);
    A.Arg1 = Vecs[I].NArgs & 0xF;      /* arginfo: count of 32-bit values */
    A.Arg2 = Vecs[I].A0;
    A.Arg3 = Vecs[I].A1;
    A.Arg4 = Vecs[I].A2;
    Smc (&A);
    Ret = A.Arg0;
    DEBUG ((DEBUG_ERROR,
            "ScmTry3: n=%u a0=%lx a1=%lx -> x0=%lx x1=%lx x2=%lx\n",
            Vecs[I].NArgs, Vecs[I].A0, Vecs[I].A1, Ret, A.Arg1, A.Arg2));
  }

  DEBUG ((DEBUG_ERROR, "ScmTry3: memory-protection calls, one argument each\n"));
  {
    STATIC CONST UINTN  Cmds[] = { 0x03, 0x1B, 0x16, 0x0E, 0x0F, 0x23 };
    for (I = 0; I < ARRAY_SIZE (Cmds); ++I) {
      ZeroMem (&A, sizeof (A));
      A.Arg0 = SCM_FNID_SIP (0x0C, Cmds[I]);
      A.Arg1 = 1;
      A.Arg2 = 0;
      Smc (&A);
      DEBUG ((DEBUG_ERROR, "ScmTry3: MP cmd %02x -> x0=%lx x1=%lx\n",
              Cmds[I], A.Arg0, A.Arg1));
    }
  }

  DEBUG ((DEBUG_ERROR, "ScmTry3: done\n"));
  return EFI_SUCCESS;
}
