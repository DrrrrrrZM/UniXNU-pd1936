# 构建镜像前的固定检查（工作规则）

每次产出 boot 镜像前逐条执行；任一条不通过就不交付。

## 1. 摘掉已知会挂住总线的探针

固件里某些实验驱动一旦运行就会让机器停在那里，后面的内核日志根本看不到。
它们必须从 DSC/FDF 里摘掉，除非这一轮**就是要测它**。

| 驱动 | 影响 | 处理 |
|---|---|---|
`UfsReadDxe` | 敲 UFS doorbell 后总线挂死，走不到内核 | 默认摘除；只在测固件侧 UFS 时临时加回 |

摘除方法（`scripts/prepare-pd1936-uefi.py`）：

```
去掉  pd1936Pkg/Driver/UfsReadDxe/UfsReadDxe.inf        （DSC [Components]）
去掉  INF pd1936Pkg/Driver/UfsReadDxe/UfsReadDxe.inf    （FDF）
```

验证：`FVMAIN.inf` 里 UfsReadDxe 计数为 0，且 `FVMAIN.Fv` 里搜不到
`list stopped` / `UfsRead: CAP` / `no completion` 这些特征串。

## 2. 内核补丁只有两处是"常态"

```
0x948c74: b 0x948cf8    跳过 Apple 固定调试控制台地址探测（没有它内核零输出）
0x7923fe0: nop          根设备匹配字典不再要求 Content
```

其他任何地址上的改动都属于实验，交付前必须确认是否需要保留。

## 3. 探针必须清干净

```
七个历史探针锚点（0x79b6eec / 0x79b6efc / 0x79b6f14 / 0x79b6f24 /
0x79b6f2c / 0x79b6ff4 / 0x79b7010）必须是原始指令
色条 0x3ec488 / 0x3ee350 必须与 kernel-src.macho 逐字节一致
```

## 4. 产物一致性

```
① 构建输出的 FD 与 pd1936/out/SILICIUM_UEFI.fd 的 md5 必须相同
② 打包用的 payload 必须是从本轮 CI 取下来的那一份
③ 镜像内的命令行要读回来核对（patch_args 之后必须回读，不能只看"写成功"）
```

## 5. 驱动里的打印方式

```
控制台尚未建立就跑的驱动（DXE 早期，如 UsbHostDxe）只能用 DEBUG()；
用 Print() 会命中 ASSERT: UefiLibPrint.c(63): Console != NULL，机器直接停。
```

## 6. 驱动分派顺序

```
必须在 BDS 之前完成的驱动（USB 主机栈、存储探测）要排在 BdsDxe 之前；
排在 BdsDxe 之后的驱动通常不会再被分派 —— UsbBusDxe/UsbMassStorageDxe 曾因此从不加载。
```

## 7. 每轮记录

```
把"这一轮要验证什么、观察到什么"写进 docs/handover-YYYY-MM-DD.md，
并把设备日志原文片段贴在对应结论旁边，避免下一轮重复试同一件事。
```
