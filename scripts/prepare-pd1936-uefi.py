#!/usr/bin/env python3
"""Prepare a Mu-Silicium (UniXNU/uefi-sheng) checkout and DTB for vivo iQOO Neo 855 / PD1936 (V1936A)."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil


pd1936_TOML = """\
#
# Boot Shim Configs
#
[boot_shim]
requires_kernel_header = true

#
# UEFI FD Configs
#
[uefi_fd]
base = 0x9FC00000
size = 0x300000

#
# Android Boot Image Kernel Configs
#
[boot_image_kernel]
kernel_compression = "gzip"
append_dtb         = true

#
# Android Boot Image Configs
#
[boot_image]
header_version = 1
"""

pd1936_DEC = """\
[Defines]
  DEC_SPECIFICATION              = 0x00010005
  PACKAGE_NAME                   = pd1936Pkg
  PACKAGE_GUID                   = 9D2E6F20-1936-4B10-A855-001936100001
  PACKAGE_VERSION                = 0.1

[Guids]
  gVendor0FD9697423AA4CDCB9CB98D17750322A = { 0x0fd96974, 0x23aa, 0x4cdc, { 0xb9, 0xcb, 0x98, 0xd1, 0x77, 0x50, 0x32, 0x2a } }
  gVendor157A5C4521B243C5BA7C822FEE5FE599 = { 0x157a5c45, 0x21b2, 0x43c5, { 0xba, 0x7c, 0x82, 0x2f, 0xee, 0x5f, 0xe5, 0x99 } }
  gVendor18A031ABB4434D1AA5C00C09261E9F71 = { 0x18a031ab, 0xb443, 0x4d1a, { 0xa5, 0xc0, 0x0c, 0x09, 0x26, 0x1e, 0x9f, 0x71 } }
  gVendor1DA97072BDDC4B3099F172A0B56FFF2A = { 0x1da97072, 0xbddc, 0x4b30, { 0x99, 0xf1, 0x72, 0xa0, 0xb5, 0x6f, 0xff, 0x2a } }
  gVendor1E5668E2848111D4BCF10080C73C8881 = { 0x1e5668e2, 0x8481, 0x11d4, { 0xbc, 0xf1, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
  gVendor241AFAE6885F4F6CA7EAC28EAB79C3E5 = { 0x241afae6, 0x885f, 0x4f6c, { 0xa7, 0xea, 0xc2, 0x8e, 0xab, 0x79, 0xc3, 0xe5 } }
  gVendor26BACCB16F4211D4BCE70080C73C8881 = { 0x26baccb1, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
  gVendor26BACCB26F4211D4BCE70080C73C8881 = { 0x26baccb2, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
  gVendor26BACCB36F4211D4BCE70080C73C8881 = { 0x26baccb3, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }
  gVendor27CFAC8746CC11D49A380090273FC14D = { 0x27cfac87, 0x46cc, 0x11d4, { 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
  gVendor27CFAC8846CC11D49A380090273FC14D = { 0x27cfac88, 0x46cc, 0x11d4, { 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
  gVendor2890B3EA053D1643AD0CD64808DA3FF1 = { 0x2890b3ea, 0x053d, 0x1643, { 0xad, 0x0c, 0xd6, 0x48, 0x08, 0xda, 0x3f, 0xf1 } }
  gVendor387477C169C711D28E3900A0C969723B = { 0x387477c1, 0x69c7, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
  gVendor47861DFF556F42699B28062EC3ECB30D = { 0x47861dff, 0x556f, 0x4269, { 0x9b, 0x28, 0x06, 0x2e, 0xc3, 0xec, 0xb3, 0x0d } }
  gVendor587E72D7CC504F798209CA291FC1A10F = { 0x587e72d7, 0xcc50, 0x4f79, { 0x82, 0x09, 0xca, 0x29, 0x1f, 0xc1, 0xa1, 0x0f } }
  gVendor5BEF7C41A560435B835B3C3EBF2BD151 = { 0x5bef7c41, 0xa560, 0x435b, { 0x83, 0x5b, 0x3c, 0x3e, 0xbf, 0x2b, 0xd1, 0x51 } }
  gVendor60759B13A8BF46FEB7E6797BFB335DF3 = { 0x60759b13, 0xa8bf, 0x46fe, { 0xb7, 0xe6, 0x79, 0x7b, 0xfb, 0x33, 0x5d, 0xf3 } }
  gVendor6441F81863624E44B5707DBA31DD2453 = { 0x6441f818, 0x6362, 0x4e44, { 0xb5, 0x70, 0x7d, 0xba, 0x31, 0xdd, 0x24, 0x53 } }
  gVendor665E3FF546CC11D49A380090273FC14D = { 0x665e3ff5, 0x46cc, 0x11d4, { 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
  gVendor665E3FF646CC11D49A380090273FC14D = { 0x665e3ff6, 0x46cc, 0x11d4, { 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }
  gVendor76B95DAD1E664D989CE7B294D9596895 = { 0x76b95dad, 0x1e66, 0x4d98, { 0x9c, 0xe7, 0xb2, 0x94, 0xd9, 0x59, 0x68, 0x95 } }
  gVendor862129360E7641C8A03A2AF2FC1C39E2 = { 0x86212936, 0x0e76, 0x41c8, { 0xa0, 0x3a, 0x2a, 0xf2, 0xfc, 0x1c, 0x39, 0xe2 } }
  gVendor964E5B21645911D28E3900A0C969723B = { 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
  gVendor9BA45B66EFA4441CA3E4ED2224786BE2 = { 0x9ba45b66, 0xefa4, 0x441c, { 0xa3, 0xe4, 0xed, 0x22, 0x24, 0x78, 0x6b, 0xe2 } }
  gVendor9E61DE7C99274F289F3D320705D81801 = { 0x9e61de7c, 0x9927, 0x4f28, { 0x9f, 0x3d, 0x32, 0x07, 0x05, 0xd8, 0x18, 0x01 } }
  gVendorA46423E3461749F1B9FFD1BFA9115839 = { 0xa46423e3, 0x4617, 0x49f1, { 0xb9, 0xff, 0xd1, 0xbf, 0xa9, 0x11, 0x58, 0x39 } }
  gVendorA966CC4FEE6C424DA7628ED3B5A4A501 = { 0xa966cc4f, 0xee6c, 0x424d, { 0xa7, 0x62, 0x8e, 0xd3, 0xb5, 0xa4, 0xa5, 0x01 } }
  gVendorAD9AEC187BF048099E963012309F3DF7 = { 0xad9aec18, 0x7bf0, 0x4809, { 0x9e, 0x96, 0x30, 0x12, 0x30, 0x9f, 0x3d, 0xf7 } }
  gVendorAE37B942457F4C91A196D9669FD347A3 = { 0xae37b942, 0x457f, 0x4c91, { 0xa1, 0x96, 0xd9, 0x66, 0x9f, 0xd3, 0x47, 0xa3 } }
  gVendorB0760469970C487AA4B528DB7B45CEF1 = { 0xb0760469, 0x970c, 0x487a, { 0xa4, 0xb5, 0x28, 0xdb, 0x7b, 0x45, 0xce, 0xf1 } }
  gVendorB27AE8B13E104D07AB5CEB9A6DC6FA8F = { 0xb27ae8b1, 0x3e10, 0x4d07, { 0xab, 0x5c, 0xeb, 0x9a, 0x6d, 0xc6, 0xfa, 0x8f } }
  gVendorB7DFB4E1052F449F87BE9818FC91B733 = { 0xb7dfb4e1, 0x052f, 0x449f, { 0x87, 0xbe, 0x98, 0x18, 0xfc, 0x91, 0xb7, 0x33 } }
  gVendorC1BFF82D6C9248FB8355C7FB4218A4EA = { 0xc1bff82d, 0x6c92, 0x48fb, { 0x83, 0x55, 0xc7, 0xfb, 0x42, 0x18, 0xa4, 0xea } }
  gVendorEB97088ECFDF49C6BE4BD906A5B20E86 = { 0xeb97088e, 0xcfdf, 0x49c6, { 0xbe, 0x4b, 0xd9, 0x06, 0xa5, 0xb2, 0x0e, 0x86 } }
  gVendorEF9FC172A1B24693B3276D32FC416042 = { 0xef9fc172, 0xa1b2, 0x4693, { 0xb3, 0x27, 0x6d, 0x32, 0xfc, 0x41, 0x60, 0x42 } }
  gVendorF4E5C7D0D23947CBAACD7F66EF763238 = { 0xf4e5c7d0, 0xd239, 0x47cb, { 0xaa, 0xcd, 0x7f, 0x66, 0xef, 0x76, 0x32, 0x38 } }
  gVendorFA5F306BF47D4AC4A47D882F8204EC30 = { 0xfa5f306b, 0xf47d, 0x4ac4, { 0xa4, 0x7d, 0x88, 0x2f, 0x82, 0x04, 0xec, 0x30 } }
"""

pd1936_DSC = """\
##
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = pd1936
  PLATFORM_GUID                  = 9D2E6F21-1936-4B10-A855-001936100001
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/pd1936Pkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = pd1936Pkg/pd1936.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0
  SHENG_VENDOR_HW                = 0

  #
  # 0 = SM8150-AB
  # 1 = SM8150-AC / 855+
  # 2 = SM8150-AC / 860
  #
  SOC_TYPE                       = 1

!include HanaPkg/HanaPkg.dsc.inc

[PcdsFixedAtBuild]
  # DDR Memory
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  # UEFI Stack (matches "UEFI Stack" in MemoryMapLib.c)
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FFB0000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x20000

  # SMBIOS
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"vivo"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"iQOO Neo 855"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"pd1936"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"vivo_iQOO_Neo855_pd1936"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"pd1936"

  # Simple Frame Buffer (1080x2340 @ 0x9C000000)
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2340
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  # Platform PEI (skip XBL shared-IMEM HOB walking during first bring-up)
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|FALSE

[LibraryClasses]
  MemoryMapLib|pd1936Pkg/Library/MemoryMapLib/MemoryMapLib.inf
  ConfigurationMapLib|pd1936Pkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  shengPkg/Drivers/ShengBootMemoryDxe/ShengBootMemoryDxe.inf
  UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  shengPkg/Drivers/ShengKeyInputDxe/ShengKeyInputDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
  pd1936Pkg/Driver/HandoffDumpDxe/HandoffDumpDxe.inf
  pd1936Pkg/Driver/WdtOffDxe/WdtOffDxe.inf
  pd1936Pkg/Driver/GopInfoDxe/GopInfoDxe.inf
  pd1936Pkg/Driver/Lz4DiskDxe/Lz4DiskDxe.inf
  pd1936Pkg/Driver/ShimFindDxe/ShimFindDxe.inf
  pd1936Pkg/Driver/ScmShmDxe/ScmShmDxe.inf
  pd1936Pkg/Driver/ScmTryDxe/ScmTryDxe.inf
  pd1936Pkg/Driver/ScmScanDxe/ScmScanDxe.inf
  pd1936Pkg/Driver/UsbHostDxe/UsbHostDxe.inf
  MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  pd1936Pkg/Driver/ExtStorageProbeDxe/ExtStorageProbeDxe.inf
"""

pd1936_FDF = """\
##
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[FD.SILICIUM_UEFI]
BaseAddress   = $(FD_BASE)|gArmTokenSpaceGuid.PcdFdBaseAddress
Size          = $(FD_SIZE)|gArmTokenSpaceGuid.PcdFdSize
ErasePolarity = 1

BlockSize     = 0x1000
NumBlocks     = $(FD_BLOCKS)

0x00000000|$(FD_SIZE)
gArmTokenSpaceGuid.PcdFvBaseAddress|gArmTokenSpaceGuid.PcdFvSize
FV = FVMAIN_COMPACT

[FV.FvMain]
FvNameGuid         = CD8D4C84-20AD-4073-8A28-3400FAE05941
BlockSize          = 0x1000
NumBlocks          = 0
FvAlignment        = 8
ERASE_POLARITY     = 1
MEMORY_MAPPED      = TRUE
STICKY_WRITE       = TRUE
LOCK_CAP           = TRUE
LOCK_STATUS        = TRUE
WRITE_DISABLED_CAP = TRUE
WRITE_ENABLED_CAP  = TRUE
WRITE_STATUS       = TRUE
WRITE_LOCK_CAP     = TRUE
WRITE_LOCK_STATUS  = TRUE
READ_DISABLED_CAP  = TRUE
READ_ENABLED_CAP   = TRUE
READ_STATUS        = TRUE
READ_LOCK_CAP      = TRUE
READ_LOCK_STATUS   = TRUE

  !include Include/APRIORI.inc
  !include Include/DXE.inc

  # RAM Manager reads the RAM partition table published by the vendor stack.
!if $(SHENG_VENDOR_HW) == 1
  INF SiliciumPkg/Drivers/RamManagerDxe/RamManagerDxe.inf
!endif

  # SmBios
  INF MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf
  INF SiliciumPkg/Drivers/SmBiosTableDxe/SmBiosTableDxe.inf

  # UniXNU
  INF UniXNUPkg/Application/UniXNU/UniXNU.inf
  INF UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf

  !include QcomPkg/Extra.fdf.inc

[FV.FVMAIN_COMPACT]
FvAlignment        = 8
ERASE_POLARITY     = 1
MEMORY_MAPPED      = TRUE
STICKY_WRITE       = TRUE
LOCK_CAP           = TRUE
LOCK_STATUS        = TRUE
WRITE_DISABLED_CAP = TRUE
WRITE_ENABLED_CAP  = TRUE
WRITE_STATUS       = TRUE
WRITE_LOCK_CAP     = TRUE
WRITE_LOCK_STATUS  = TRUE
READ_DISABLED_CAP  = TRUE
READ_ENABLED_CAP   = TRUE
READ_STATUS        = TRUE
READ_LOCK_CAP      = TRUE
READ_LOCK_STATUS   = TRUE

  INF SiliciumPkg/Sec/Sec.inf

  FILE FV_IMAGE = 9E21FD93-9C72-4c15-8C4B-E77F1DB2D792 {
    SECTION GUIDED EE4E5898-3914-4259-9D6E-DC7BD79403CF PROCESSING_REQUIRED = TRUE {
      SECTION FV_IMAGE = FVMAIN
    }
  }

  !include SiliciumPkg/Common.fdf.inc
"""

pd1936_APRIORI = """\
APRIORI DXE {
  # First, install the PCD driver and call DxeInit
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf

  # Next, install cpu protocol and enable the interrupt controller
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf

  # Core Drivers
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
}
"""

pd1936_DXE = """\
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF pd1936Pkg/Driver/HandoffDumpDxe/HandoffDumpDxe.inf
  INF pd1936Pkg/Driver/WdtOffDxe/WdtOffDxe.inf
  INF pd1936Pkg/Driver/GopInfoDxe/GopInfoDxe.inf
  INF pd1936Pkg/Driver/Lz4DiskDxe/Lz4DiskDxe.inf
  INF pd1936Pkg/Driver/ShimFindDxe/ShimFindDxe.inf
  INF pd1936Pkg/Driver/ScmShmDxe/ScmShmDxe.inf
  INF pd1936Pkg/Driver/ScmTryDxe/ScmTryDxe.inf
  INF pd1936Pkg/Driver/ScmScanDxe/ScmScanDxe.inf
  INF pd1936Pkg/Driver/UsbHostDxe/UsbHostDxe.inf
  INF MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceDxe.inf
  INF MdeModulePkg/Bus/Pci/XhciDxe/XhciDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  INF pd1936Pkg/Driver/ExtStorageProbeDxe/ExtStorageProbeDxe.inf
  # bring-up: one SCSI READ(10) on LUN0

  INF shengPkg/Drivers/ShengBootMemoryDxe/ShengBootMemoryDxe.inf
  INF UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF shengPkg/Drivers/ShengKeyInputDxe/ShengKeyInputDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf

"""

MEMORY_MAP_INF = """\
[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = MemoryMapLib
  FILE_GUID                      = 375C4FF0-1905-4BA7-A5CA-7D40D25F5EF3
  MODULE_TYPE                    = BASE
  VERSION_STRING                 = 1.0
  LIBRARY_CLASS                  = MemoryMapLib

[Sources]
  MemoryMapLib.c

[Packages]
  ArmPkg/ArmPkg.dec
  MdePkg/MdePkg.dec
  SiliciumPkg/SiliciumPkg.dec
"""

MEMORY_MAP_C = """\
// SPDX-License-Identifier: BSD-2-Clause-Patent
// Memory map for vivo iQOO Neo 855 (pd1936, SM8150 v2)
// Carveouts measured from xbl.elf and /proc/iomem (see pd1936/hardware-inventory.md).
#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  // DDR Regions - measured from /proc/iomem (9 System RAM ranges) and the
  // xbl.elf memory map. Contiguous and non-overlapping; every entry ends where
  // the next begins so ValidateMemoryMap() in MemoryInitPeiLib is satisfied.
  {"Payload RAM",        0x80000000, 0x04B00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"RAM 0x84B0",         0x84B00000, 0x00C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"HYP",                0x85700000, 0x00600000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, NS_DEVICE},
  {"RAM 0x85D0",         0x85D00000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"XBL_AOP",            0x85E00000, 0x00120000, AddMem, MEM_RES, WRITE_COMBINEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"AOP CMD DB",         0x85F20000, 0x00020000, AddMem, MEM_RES, WRITE_COMBINEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"RAM 0x85F4",         0x85F40000, 0x000C0000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"SMEM",               0x86000000, 0x00200000, AddMem, MEM_RES, WRITE_COMBINEABLE, Reserv, UNCACHED_UNBUFFERED},
  {"QTEE",               0x86200000, 0x05500000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"PIL Reserved",       0x8B700000, 0x00100000, AddMem, MEM_RES, WRITE_COMBINEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"PIL Reserved II",    0x8B800000, 0x0E600000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, UNCACHED_UNBUFFERED_XN},
  {"DXE Heap",           0x99E00000, 0x01C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"Sched Heap",         0x9BA00000, 0x00600000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Display Reserved",   0x9C000000, 0x02400000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
  {"TGCM",               0x9E400000, 0x01400000, AddMem, MEM_RES, WRITE_COMBINEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"FV Region",          0x9F800000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"ABOOT FV",           0x9FA00000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"UEFI FD",            0x9FC00000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"SEC Heap",           0x9FF00000, 0x0008C000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"CPU Vectors",        0x9FF8C000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"MMU PageTables",     0x9FF8D000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"UEFI Stack",         0x9FF90000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Runtime Data",       0x9FFD0000, 0x00027000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"Log Buffer",         0x9FFF7000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"Info Blk",           0x9FFFF000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"MLVM_APSS",          0xA0000000, 0x04C00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"CDSP_SEC",           0xA4C00000, 0x03C00000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"MLVM_1",             0xA8800000, 0x07C00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"MLVM",               0xB0400000, 0x09000000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"RAM 0xB940",         0xB9400000, 0x02700000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  // Reserved for the payload volume appended to the kernel field (embed_payload):
  // BootShim copies it to 0xC0000000, so the firmware can mount the FAT volume from
  // a fixed address and ramdisk_size stays 0.
  {"Payload",            0xC0000000, 0x04B00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"RAM 0xC4B0",         0xC4B00000, 0x3B206000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},
  {"Payload High",       0x100000000,0x00C800000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"RAM High",           0x10C800000,0x173800000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

  // Other Memory Regions
  {"IMEM Base",          0x14680000, 0x00040000, NoHob,  MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},

  // Register Regions (SM8150 / Coarse non-overlapping blocks < 64 total entries)
  {"GCC_SECURITY_PRNG",  0x00100000, 0x00700000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"QUP",                0x00800000, 0x00500000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PCIE_UFS_CRYPTO",    0x01C00000, 0x00400000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GPU_REGS",           0x02C00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"TLMM",               0x03000000, 0x01000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SYS_PERIPH_MCCC",    0x08500000, 0x00C00000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB",                0x0A600000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"DISP_MDSS",          0x0AB00000, 0x00500000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"AOSS_RPMH_PMIC",     0x0B000000, 0x04000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"SMMU",               0x15000000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"APSS_SUBSYS",        0x17A00000, 0x00B00000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"PCIE_AXI",           0x40000000, 0x35000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
"""

CONFIG_MAP_INF = """\
[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = ConfigurationMapLib
  FILE_GUID                      = D7DDE228-62E4-43E3-B5A1-AB29FB9B0A35
  MODULE_TYPE                    = BASE
  VERSION_STRING                 = 1.0
  LIBRARY_CLASS                  = ConfigurationMapLib

[Sources]
  ConfigurationMapLib.c

[Packages]
  MdePkg/MdePkg.dec
  QcomPkg/QcomPkg.dec
"""

CONFIG_MAP_C = """\
#include <Library/ConfigurationMapLib.h>

STATIC
EFI_CONFIGURATION_ENTRY_DESCRIPTOR
gConfigurationDescriptor[] = {
  {"AllowNonPersistentVarsInRetail", 0x1},
  {"DDRInfoNotifyFlag", 0x0},
  {"DRAM_CLK_PERIOD_ADDR", 0x240BA050},
  {"DetectRetailUserAttentionHotkey", 0x0},
  {"DetectRetailUserAttentionHotkeyCode", 0x17},
  {"DloadCookieAddr", 0x1FD9000},
  {"DloadCookieValue", 0x30},
  {"EarlyInitCoreCnt", 0x1},
  {"EnableACPIFallback", 0x0},
  {"EnableDisplayImageFv", 0x0},
  {"EnableDisplayThread", 0x0},
  {"EnableLogFsSyncInRetail", 0x0},
  {"EnableMultiCoreFvDecompression", 0x0},
  {"EnableMultiThreading", 0x0},
  {"EnableOEMSetupAppInRetail", 0x0},
  {"EnableSDHCSwitch", 0x1},
  {"EnableShell", 0x1},
  {"EnableUefiSecAppDebugLogDump", 0x0},
  {"EnableUfsIOC", 0x1},
  {"EnableVariablePolicyEngine", 0x0},
  {"LoadAutoImageInPILFlag", 0x1},
  {"MaxCoreCnt", 0x8},
  {"MaxLogFileSize", 0x400000},
  {"NumActiveCores", 0x1},
  {"NumCpus", 0x8},
  {"PilSubsysDbgCookieAddr", 0x146AA6DC},
  {"PilSubsysDbgCookieVal", 0x53444247},
  {"PwrBtnShutdownFlag", 0x0},
  {"Sdc1GpioConfigOff", 0xA00},
  {"Sdc1GpioConfigOn", 0x1E92},
  {"Sdc2GpioConfigOff", 0xA00},
  {"Sdc2GpioConfigOn", 0x1E92},
  {"SecPagePoolCount", 0x800},
  {"SecurityFlag", 0xC4},
  {"SharedIMEMBaseAddr", 0x146AA000},
  {"ShmBridgememSize", 0xA00000},
  {"USBHS1_Config", 0x0},
  {"UefiMemUseThreshold", 0xE1},
  {"UfsSmmuConfigForOtherBootDev", 0x1},
  {"UsbFnIoRevNum", 0x10001}
};

VOID
GetConfigurationMap (
  OUT EFI_CONFIGURATION_ENTRY_DESCRIPTOR **ConfigurationDescriptor,
  OUT UINT8                               *ConfigurationDescriptorCount)
{
  *ConfigurationDescriptor      = gConfigurationDescriptor;
  *ConfigurationDescriptorCount = ARRAY_SIZE (gConfigurationDescriptor);
}
"""


MMU_DIAG_ANCHOR = '  DEBUG ((EFI_D_WARN, "MMU: HOBs built, enabling\\n"));'
MMU_DIAG_CODE = """\
  // pd1936 bring-up diagnostics: show that the HOB list reads back and how many MMU entries are passed.
  {
    EFI_HOB_HANDOFF_INFO_TABLE *Phit = (EFI_HOB_HANDOFF_INFO_TABLE *)GetHobList ();
    DEBUG ((EFI_D_WARN, "MMU: PHIT %p type 0x%x free 0x%lx-0x%lx\\n", Phit, (UINT32)Phit->Header.HobType, (UINT64)Phit->EfiFreeMemoryBottom, (UINT64)Phit->EfiFreeMemoryTop));
    DEBUG ((EFI_D_WARN, "MMU: %u table entries, last 0x%lx+0x%lx\\n", (UINT32)Index, (UINT64)MemoryTable[Index ? Index - 1 : 0].PhysicalBase, (UINT64)MemoryTable[Index ? Index - 1 : 0].Length));
  }
"""


def patch_mmu_diagnostics(uefi_dir: Path) -> None:
    src = uefi_dir / "Silicon" / "Silicium" / "SiliciumPkg" / "Library" / "MemoryInitPeiLib" / "MemoryInitPei.c"
    if not src.is_file():
        print(f"WARNING: {src} not found, MMU diagnostics not added")
        return
    text = src.read_text(encoding="utf-8")
    if "pd1936 bring-up diagnostics" in text:
        return
    if MMU_DIAG_ANCHOR not in text:
        print("WARNING: MMU diagnostics anchor not found, skipped")
        return
    src.write_text(text.replace(MMU_DIAG_ANCHOR, MMU_DIAG_CODE + MMU_DIAG_ANCHOR, 1), encoding="utf-8")
    print(f"Added MMU diagnostics to {src}")


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--uefi",
        type=Path,
        default=Path(os.environ["UNIXNU_UEFI"]) if "UNIXNU_UEFI" in os.environ else None,
        help="Path to Mu-Silicium (UniXNU/uefi-sheng) checkout",
    )
    parser.add_argument(
        "--device",
        type=Path,
        default=Path(os.environ["UNIXNU_DEVICE"])
        if "UNIXNU_DEVICE" in os.environ
        else Path(__file__).resolve().parent.parent,
        help="Path to UniXNU-pd1936 device repository",
    )
    parser.add_argument(
        "--tree",
        type=Path,
        default=Path.cwd(),
        help="Path to UniXNU root repository (where build/pd1936/pd1936.dtb is staged)",
    )
    args = parser.parse_args()

    device_dir = args.device.resolve()
    src_dtb = device_dir / "dts" / "vivo-pd1936.dtb"
    if not src_dtb.is_file():
        raise SystemExit(f"Missing device DTB: {src_dtb}")

    # Stage build/pd1936/pd1936.dtb under the active build tree
    tree_dir = args.tree.resolve()
    dst_dtb = tree_dir / "build" / "pd1936" / "pd1936.dtb"
    dst_dtb.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_dtb, dst_dtb)
    print(f"Staged DTB: {dst_dtb} ({dst_dtb.stat().st_size} bytes)")

    if args.uefi is not None:
        uefi_dir = args.uefi.resolve()
        if not (uefi_dir / "Resources" / "Scripts" / "mkbootimg.py").is_file():
            raise SystemExit(f"Not a Mu-Silicium checkout: {uefi_dir}")

        uefi_dtb = uefi_dir / "Resources" / "DTBs" / "pd1936.dtb"
        uefi_dtb.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_dtb, uefi_dtb)

        write_text(uefi_dir / "Resources" / "Configs" / "pd1936.toml", pd1936_TOML)
        pkg_dir = uefi_dir / "Platforms" / "VIVO" / "pd1936Pkg"
        write_text(pkg_dir / "pd1936.dec", pd1936_DEC)
        write_text(pkg_dir / "pd1936.dsc", pd1936_DSC)
        write_text(pkg_dir / "pd1936.fdf", pd1936_FDF)
        write_text(pkg_dir / "Include" / "APRIORI.inc", pd1936_APRIORI)
        write_text(pkg_dir / "Include" / "DXE.inc", pd1936_DXE)

        bootinfo_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "BootInfoDumpDxe"
        if bootinfo_src.is_dir():
            dst = pkg_dir / "Driver" / "BootInfoDumpDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in bootinfo_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        handoff_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "HandoffDumpDxe"
        if handoff_src.is_dir():
            dst = pkg_dir / "Driver" / "HandoffDumpDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in handoff_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        wdtoff_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "WdtOffDxe"
        if wdtoff_src.is_dir():
            dst = pkg_dir / "Driver" / "WdtOffDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in wdtoff_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        gopinfo_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "GopInfoDxe"
        if gopinfo_src.is_dir():
            dst = pkg_dir / "Driver" / "GopInfoDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in gopinfo_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        lz4disk_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "Lz4DiskDxe"
        if lz4disk_src.is_dir():
            dst = pkg_dir / "Driver" / "Lz4DiskDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in lz4disk_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        shimfind_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "ShimFindDxe"
        if shimfind_src.is_dir():
            dst = pkg_dir / "Driver" / "ShimFindDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in shimfind_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        scmshm_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "ScmShmDxe"
        if scmshm_src.is_dir():
            dst = pkg_dir / "Driver" / "ScmShmDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in scmshm_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        scmtry_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "ScmTryDxe"
        if scmtry_src.is_dir():
            dst = pkg_dir / "Driver" / "ScmTryDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in scmtry_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        scmscan_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "ScmScanDxe"
        if scmscan_src.is_dir():
            dst = pkg_dir / "Driver" / "ScmScanDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in scmscan_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        usbhost_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "UsbHostDxe"

        extstor_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "ExtStorageProbeDxe"
        if extstor_src.is_dir():
            dst = pkg_dir / "Driver" / "ExtStorageProbeDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in extstor_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)
        if usbhost_src.is_dir():
            dst = pkg_dir / "Driver" / "UsbHostDxe"
            dst.mkdir(parents=True, exist_ok=True)
            for item in usbhost_src.iterdir():
                if item.is_file():
                    shutil.copy2(item, dst / item.name)

        vendor_platform = (Path(__file__).resolve().parents[1].parent
                           / "vendor" / "iqoo-pd1936" / "platform")
        if vendor_platform.is_dir():
            for item in vendor_platform.iterdir():
                if item.suffix.lower() in (".efi", ".inf"):
                    dst = pkg_dir / "Driver" / "VendorQcom" / item.stem
                    dst.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(item, dst / item.name)

        vendor_efi = Path(__file__).resolve().parents[1].parent / "vendor" / "iqoo-pd1936" / "UFSDxe.efi"
        if vendor_efi.is_file():
            dst = pkg_dir / "Driver" / "VendorUfsDxe"
            dst.mkdir(parents=True, exist_ok=True)
            shutil.copy2(vendor_efi, dst / "UFSDxe.efi")

        driver_src = Path(__file__).resolve().parents[1] / "uefi-image" / "drivers" / "UfsReadDxe"
        if driver_src.is_dir():
            driver_dst = pkg_dir / "Driver" / "UfsReadDxe"
            driver_dst.mkdir(parents=True, exist_ok=True)
            for item in driver_src.iterdir():
                if item.is_file():
                    (driver_dst / item.name).write_text(
                        item.read_text(encoding="utf-8"), encoding="utf-8")

        write_text(pkg_dir / "Library" / "MemoryMapLib" / "MemoryMapLib.inf", MEMORY_MAP_INF)
        write_text(pkg_dir / "Library" / "MemoryMapLib" / "MemoryMapLib.c", MEMORY_MAP_C)
        write_text(
            pkg_dir / "Library" / "ConfigurationMapLib" / "ConfigurationMapLib.inf",
            CONFIG_MAP_INF,
        )
        write_text(
            pkg_dir / "Library" / "ConfigurationMapLib" / "ConfigurationMapLib.c",
            CONFIG_MAP_C,
        )
        patch_mmu_diagnostics(uefi_dir)
        print(f"Injected pd1936Pkg into {uefi_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
