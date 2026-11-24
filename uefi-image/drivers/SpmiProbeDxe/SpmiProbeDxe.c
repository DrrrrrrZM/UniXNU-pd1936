/** @file
  SPMI bring-up probe for PD1936: verify that the PMIC arbiter can be used at all, by
  looking up an APID for a known PPID and reading a PMIC register.

  Layout taken from Linux drivers/spmi/spmi-pmic-arb.c (arbiter v5, our core reads 0x50000000):
    version         core.00
    APID map        core.900 + 4*apid       (entry holds the PPID in bits[11:0])
    write channel   chnls.10000*apid        (.00 CMD, .10 WDATA0, .14 WDATA1, .08 STATUS)
    read  channel   obsrvr.10000*ee + 0x80*apid (.00 CMD, .18 RDATA0, .1c RDATA1, .08 STATUS)
    command         (opc << 27) | ((addr & 0xFF) << 4) | (bc & 7)
  The PMIC slaves here: PM8150 at USID 0, PM8150B at USID 2 (from the device tree).

  This build only reads. Enabling VBUS means writing PM8150 GPIO9 (usb2_vbus_boost) and that
  needs a proven read path first.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define SPMI_CORE    0x0C440000UL
#define SPMI_CHNLS   0x0C600000UL
#define SPMI_OBS     0x0E600000UL
#define APID_MAP     0x900UL

STATIC
UINT16
FindApid (
  IN UINT16  PpId
  )
{
  UINT32  I;
  UINT32  V;

  for (I = 0; I < 512; ++I) {
    V = MmioRead32 (SPMI_CORE + APID_MAP + 4 * I);
    if ((((V >> 8) & 0xFFF) == PpId) && (V != 0xFFFFFFFFu)) {
      return (UINT16)I;
    }
  }
  return 0xFFFF;
}

STATIC
UINT32
SpmiWriteByte (
  IN UINT16  Apid,
  IN UINT8   Addr,
  IN UINT8   Value
  )
{
  UINT32  Base = SPMI_CHNLS + 0x10000 * Apid;
  UINT32  Cmd  = (1u << 27) | (((UINT32)Addr & 0xFF) << 4) | 0;
  UINT32  N;

  MmioWrite32 (Base + 0x10, Value);        /* WDATA0 */
  MmioWrite32 (Base + 0x00, Cmd);          /* doorbell */
  for (N = 0; N < 100; ++N) {
    if ((MmioRead32 (Base + 0x08) & 0xFF) != 0xFF) {
      break;
    }
    gBS->Stall (100);
  }
  return MmioRead32 (Base + 0x08);
}

STATIC
UINT32
SpmiReadByte (
  IN UINT16  Apid,
  IN UINT8   Addr
  )
{
  UINT32  Base = SPMI_OBS + 0x80 * Apid;
  UINT32  Cmd  = (0u << 27) | (((UINT32)Addr & 0xFF) << 4) | 0;
  UINT32  N;

  MmioWrite32 (Base + 0x00, Cmd);
  for (N = 0; N < 100; ++N) {
    if ((MmioRead32 (Base + 0x08) & 0xFF) != 0xFF) {
      break;
    }
    gBS->Stall (100);
  }
  return MmioRead32 (Base + 0x18);
}

EFI_STATUS
EFIAPI
SpmiProbeDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  Ver;
  UINT32  Feat;
  UINT32  I;
  UINT32  V;
  UINT16  ApidRev;
  UINT16  ApidGpio;
  UINT32  PpIdRev  = (0u << 8) | (0x0100u >> 8);    /* PM8150 revision page  */
  UINT32  PpIdGpio = (0u << 8) | (0xC845u >> 8);    /* PM8150 GPIO9 out ctrl */
  UINT32  Opc;
  UINT32  Cmd;
  UINT32  Sts;
  UINT32  Data;
  BOOLEAN AnyOk = FALSE;

  Ver  = MmioRead32 (SPMI_CORE + 0x00);
  Feat = MmioRead32 (SPMI_CORE + 0x04);
  DEBUG ((DEBUG_ERROR, "Spmi: version=%08x features=%08x\n", Ver, Feat));

  /* show the first few valid APID map entries so the table base can be confirmed */
  for (I = 0; I < 512 && I < 200; ++I) {
    V = MmioRead32 (SPMI_CORE + APID_MAP + 4 * I);
    if ((V & 0x8000) != 0) {
      DEBUG ((DEBUG_ERROR, "Spmi: apid[%u] -> ppid %u (raw %08x)\n",
              I, (V >> 8) & 0xFFF, V));
    }
  }

  ApidRev  = FindApid ((UINT16)PpIdRev);
  ApidGpio = FindApid ((UINT16)PpIdGpio);
  DEBUG ((DEBUG_ERROR, "Spmi: ppid %u (rev) -> apid %u, ppid %u (gpio9) -> apid %u\n",
          PpIdRev, ApidRev, PpIdGpio, ApidGpio));

  if (ApidRev == 0xFFFF) {
    DEBUG ((DEBUG_ERROR, "Spmi: no APID for the PM8150 revision page, aborting\n"));
    return EFI_SUCCESS;
  }

  /* sweep EE index and opcode: the observation window is obsrvr + 0x10000*ee + 0x80*apid */
  {
    UINT32  Ee;
    UINT32  Base;
    for (Ee = 0; Ee < 8 && !AnyOk; ++Ee) {
      for (Opc = 0; Opc < 8 && !AnyOk; ++Opc) {
        Base = SPMI_OBS + 0x10000 * Ee + 0x80 * ApidGpio;
        Cmd  = (Opc << 27) | ((0xC845u & 0xFF) << 4) | 0;
        MmioWrite32 (Base + 0x00, Cmd);
        gBS->Stall (1000);
        Sts  = MmioRead32 (Base + 0x08);
        Data = MmioRead32 (Base + 0x18);
        if ((Sts != 0xFFFFFFFFu) || (Data != 0xFFFFFFFFu)) {
          DEBUG ((DEBUG_ERROR,
                  "Spmi: ee=%u opc=%u cmd=%08x status=%08x data=%08x\n",
                  Ee, Opc, Cmd, Sts, Data));
        }
        if (((Sts >> 8) & 0x7) == 0x0 && (Sts & 0xFF) != 0xFF) {
          AnyOk = TRUE;
          DEBUG ((DEBUG_ERROR, "Spmi: READ PATH WORKS ee=%u opc=%u data=%08x\n",
                  Ee, Opc, Data));
        }
      }
    }
  }

  if (!AnyOk) {
    DEBUG ((DEBUG_ERROR, "Spmi: read path not proven, not writing\n"));
    return EFI_SUCCESS;
  }

  if (ImageHandle == NULL) {
    (VOID)SpmiWriteByte (0, 0, 0);
  }

  /* ---- find every APID for the GPIO9 page and work out which one writes ---- */
  {
    UINT16  Apids[16];
    UINT32  Raw[16];
    UINT32  N = 0;
    UINT32  K;
    UINT32  Opc2;
    UINT32  Before;
    UINT32  After;
    UINT32  Base;

    for (K = 0; K < 512 && N < 16; ++K) {
      V = MmioRead32 (SPMI_CORE + APID_MAP + 4 * K);
      if ((V != 0xFFFFFFFFu) && (((V >> 8) & 0xFFF) == PpIdGpio)) {
        Apids[N] = (UINT16)K;
        Raw[N]   = V;
        DEBUG ((DEBUG_ERROR, "Spmi: ppid200 <- apid %u raw %08x\n", K, V));
        ++N;
      }
    }
    DEBUG ((DEBUG_ERROR, "Spmi: %u apid(s) for ppid 200\n", N));

    for (K = 0; K < N; ++K) {
      DEBUG ((DEBUG_ERROR, "Spmi: apid %u reads: 40=%08x 45=%08x 46=%08x\n",
              Apids[K],
              MmioRead32 (SPMI_OBS + 0x80 * Apids[K] + 0x18),
              MmioRead32 (SPMI_OBS + 0x80 * Apids[K] + 0x18),
              MmioRead32 (SPMI_OBS + 0x80 * Apids[K] + 0x18)));

      for (Opc2 = 0; Opc2 < 5; ++Opc2) {
        Base   = SPMI_CHNLS + 0x10000 * Apids[K];
        Before = SpmiReadByte (Apids[K], 0x45);
        MmioWrite32 (Base + 0x10, 0x01);                       /* WDATA0 = 1 (drive high) */
        MmioWrite32 (Base + 0x00,
                     (Opc2 << 27) | ((0x45u & 0xFF) << 4) | 0); /* single byte write */
        gBS->Stall (2000);
        After = SpmiReadByte (Apids[K], 0x45);
        DEBUG ((DEBUG_ERROR, "Spmi: apid %u opc %u write45: before=%08x after=%08x\n",
                Apids[K], Opc2, Before, After));
        if ((Before != After) && (After == 0x01)) {
          DEBUG ((DEBUG_ERROR, "Spmi: *** apid %u opc %u DROVE GPIO9 HIGH ***\n",
                  Apids[K], Opc2));
        }
      }
    }
  }

  return EFI_SUCCESS;
}
