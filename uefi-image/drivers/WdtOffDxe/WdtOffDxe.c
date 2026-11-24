/** @file
  Disarm the firmware watchdog so it cannot reset the machine while XNU runs.

  The DXE core arms the EFI watchdog (default period, minutes) at startup; while boot services are
  alive the timer event keeps calling the platform handler, and once DarwinBoot exits boot services
  nothing strokes it any more. The observable symptom is a machine that runs the menu indefinitely
  but resets a fixed number of seconds after the kernel starts, with the screen frozen on the last
  kernel line.

  Calling SetWatchdogTimer(0, ...) means "no watchdog", which the core honours: the handler is not
  reinstated later by anything we load.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
WdtOffDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->SetWatchdogTimer (0, 0, 0, NULL);
  DEBUG ((DEBUG_ERROR, "WdtOff: SetWatchdogTimer(0) -> %r\n", Status));
  return EFI_SUCCESS;
}
