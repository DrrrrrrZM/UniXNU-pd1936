/** @file
  Shm-bridge SCM probes (QCOM_SCM_SVC_MP 0x0C):
    0x1C ENABLE : no arguments, returns res[0]
    0x1D DELETE : 1 argument (handle)
    0x1E CREATE : 4 arguments (pfn_and_ns_perm_flags, ipfn_and_s_perm_flags, size_and_flags,
                  ns_vmids), returns status in res[0] and a handle in res[1]

  Why this matters: the vendor XBL configures a 10 MiB shm-bridge pool
  ("ShmBridgememSize = 0xA00000" in xbl.elf) and the vendor DXE drivers allocate from it. That
  pool is the piece of XBL runtime missing from a BootShim payload, and ENABLE is the call a
  non-secure OS is expected to make (Linux does exactly this in qcom-scm).

  All calls go through ArmSmcLib: a hand-written `smc #0` block corrupted registers on return and
  made the next DEBUG call assert inside DebugLib.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define SCM_SIP_FNID(s, c)  ((UINTN)0x80000000 | ((UINTN)2 << 24) | \
                             ((((UINTN)(s)) & 0xFF) << 8) | (((UINTN)(c)) & 0xFF))

STATIC
VOID
Show (
  IN CONST CHAR8  *Label,
  IN ARM_SMC_ARGS *A
  )
{
  DEBUG ((DEBUG_ERROR, "ShmBr: %-18a x0=%lx x1=%lx x2=%lx x3=%lx\n",
          Label, A->Arg0, A->Arg1, A->Arg2, A->Arg3));
}

EFI_STATUS
EFIAPI
ScmShmDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  ARM_SMC_ARGS  A;

  DEBUG ((DEBUG_ERROR, "ShmBr: shm-bridge SCM probes (svc 0x0c)\n"));

  /* 1. ENABLE: no arguments. On a device whose XBL already created the pool this is the call a
        non-secure OS makes. */
  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x1C);
  ArmCallSmc (&A);
  Show ("ENABLE", &A);

  /* 2. CREATE with zeros: tells us whether the refusal is about arguments or about permission */
  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x1E);
  A.Arg1 = 4;                    /* arginfo: four 32-bit values */
  A.Arg2 = 0;                    /* pfn_and_ns_perm_flags */
  A.Arg3 = 0;                    /* ipfn_and_s_perm_flags */
  A.Arg4 = 0;                    /* size_and_flags */
  A.Arg5 = 0;                    /* ns_vmids */
  ArmCallSmc (&A);
  Show ("CREATE zeros", &A);

  /* 3. DELETE(0) */
  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x1D);
  A.Arg1 = 1;
  A.Arg2 = 0;
  ArmCallSmc (&A);
  Show ("DELETE(0)", &A);

  /* 4. the neighbours we never tried individually */
  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x1F);
  A.Arg1 = 0;
  ArmCallSmc (&A);
  Show ("MP 0x1F", &A);

  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x17);
  A.Arg1 = 0;
  ArmCallSmc (&A);
  Show ("MP 0x17", &A);

  ZeroMem (&A, sizeof (A));
  A.Arg0 = SCM_SIP_FNID (0x0C, 0x18);
  A.Arg1 = 0;
  ArmCallSmc (&A);
  Show ("MP 0x18", &A);

  DEBUG ((DEBUG_ERROR, "ShmBr: done\n"));
  return EFI_SUCCESS;
}
