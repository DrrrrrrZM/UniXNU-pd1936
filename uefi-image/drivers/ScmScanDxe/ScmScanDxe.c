/** @file
  Enumerate the SCM surface this device's TZ exposes, using only the side-effect-free
  IS_CALL_AVAIL query.

    x0 = 0x80000000 | (OWNER_SIP << 24) | ((SVC_INFO << 8) | INFO_IS_CALL_AVAIL)
    x1 = arginfo (1 argument)
    x2 = (OWNER_SIP << 24) | ((svc << 8) | cmd)      <- the call being asked about
    returns x0 = status, x1 = 1 if implemented

  A hit under QCOM_SCM_SVC_MP (0x0C) would mean the TZ exposes a memory-protection entry
  point to a non-secure caller, which is the only way to lift the UFS/XPU fence without
  compromising EL3.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define SCM_FNID(s, c)      ((((UINT64)(s) & 0xFF) << 8) | ((UINT64)(c) & 0xFF))
#define SCM_SIP_FNID(s, c)  (0x80000000ULL | (2ULL << 24) | SCM_FNID (s, c))

#define SCM_SVC_INFO          0x06ULL
#define SCM_INFO_IS_CALL_AVAIL 0x01ULL

STATIC
UINT64
ScmCall4 (
  IN  UINT64  A0,
  IN  UINT64  A1,
  IN  UINT64  A2,
  IN  UINT64  A3,
  OUT UINT64  *Out1
  )
{
  register UINT64  X0 __asm__ ("x0") = A0;
  register UINT64  X1 __asm__ ("x1") = A1;
  register UINT64  X2 __asm__ ("x2") = A2;
  register UINT64  X3 __asm__ ("x3") = A3;

  __asm__ __volatile__ ("smc #0"
                        : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3)
                        :
                        : "x4", "x5", "x6", "x7", "memory");
  if (Out1 != NULL) {
    *Out1 = X1;
  }

  return X0;
}

EFI_STATUS
EFIAPI
ScmScanDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  Svc;
  UINT32  Cmd;
  UINT64  St;
  UINT64  Avail;
  UINT64  Bitmap;
  UINT32  Hits;
  UINT32  Total = 0;

  DEBUG ((DEBUG_ERROR, "ScmScan: IS_CALL_AVAIL survey, one line per service\n"));

  for (Svc = 0; Svc <= 0x30; ++Svc) {
    Bitmap = 0;
    Hits   = 0;
    for (Cmd = 0; Cmd <= 0x30; ++Cmd) {
      Avail = 0;
      St = ScmCall4 (SCM_SIP_FNID (SCM_SVC_INFO, SCM_INFO_IS_CALL_AVAIL), 1,
                     (2ULL << 24) | SCM_FNID (Svc, Cmd), 0, &Avail);
      if ((St == 0) && ((Avail & 0xFF) == 1)) {
        Bitmap |= (1ULL << (Cmd & 0x3F));
        ++Hits;
      }
    }
    if (Hits != 0) {
      Total += Hits;
      DEBUG ((DEBUG_ERROR, "ScmScan: svc %02x n=%u cmds=%016lx\n",
              Svc, Hits, Bitmap));
      /* spell out the memory-protection and qseecom services, they matter most */
      if ((Svc == 0x0C) || (Svc == 0x05) || (Svc == 0x0B) || (Svc == 0x02)) {
        for (Cmd = 0; Cmd <= 0x30; ++Cmd) {
          if ((Bitmap & (1ULL << (Cmd & 0x3F))) != 0) {
            DEBUG ((DEBUG_ERROR, "ScmScan:   svc %02x cmd %02x\n", Svc, Cmd));
          }
        }
      }
    }
  }

  DEBUG ((DEBUG_ERROR, "ScmScan: total %u calls available\n", Total));
  return EFI_SUCCESS;
}
