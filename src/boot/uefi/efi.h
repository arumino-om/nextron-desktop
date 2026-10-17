/*
 * efi.h — UEFI の最小限の宣言
 *
 * UEFI 仕様 2.x の構造体・プロトコルのうち、ローダが使うものだけを
 * 自前で宣言する。
 *
 * 【なぜ自前で書くか】gnu-efi (BSD) や EDK2 (BSD + 特許条項) を取り込むと
 * ライセンスの検討が増える。UEFI は**インタフェース仕様**なので、
 * 宣言を自分で書くことは仕様準拠の実装であって流用ではない。
 * docs/design-issues.md §1 の「守る線」と規律 R15 の方針に合わせる。
 *
 * 【規律】関数ポインタテーブルは**順序と個数が ABI** である。使わない
 * メンバも省略せずに並べること。省略すると後続メンバのオフセットがずれ、
 * 無言で別の関数を呼ぶことになる。
 *
 * 呼出規約は arm64 は AAPCS64、x86_64 は Microsoft x64。clang の
 * *-unknown-windows ターゲットでビルドすることで両方とも一致する。
 */

#ifndef __BTRON_UEFI_EFI_H__
#define __BTRON_UEFI_EFI_H__

#include <btron/typedef.h>

typedef UH EFI_CHAR16; /* -fshort-wchar により L"..." がこの型になる */
typedef UD EFI_STATUS; /* 仕様上は UINTN。64bit 環境なので UD */
typedef UD EFI_UINTN;
typedef VP EFI_HANDLE;
typedef UD EFI_PHYSICAL_ADDRESS;
typedef UD EFI_VIRTUAL_ADDRESS;

#define EFI_SUCCESS 0ULL
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_INVALID_PARAMETER 0x8000000000000002ULL
#define EFI_UNSUPPORTED 0x8000000000000003ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_NOT_FOUND 0x800000000000000EULL

#define EFI_ERROR(s) (((s) >> 63) != 0)

typedef struct EFI_GUID {
	UW d1;
	UH d2;
	UH d3;
	UB d4[8];
} EFI_GUID;

typedef struct EFI_TABLE_HEADER {
	UD Signature;
	UW Revision;
	UW HeaderSize;
	UW CRC32;
	UW Reserved;
} EFI_TABLE_HEADER;

/* ------------------------------------------------------------------ */
/* メモリ                                                              */
/* ------------------------------------------------------------------ */

/* EFI_ALLOCATE_TYPE */
#define AllocateAnyPages 0
#define AllocateMaxAddress 1
#define AllocateAddress 2

/* EFI_MEMORY_TYPE */
#define EfiReservedMemoryType 0
#define EfiLoaderCode 1
#define EfiLoaderData 2
#define EfiBootServicesCode 3
#define EfiBootServicesData 4
#define EfiRuntimeServicesCode 5
#define EfiRuntimeServicesData 6
#define EfiConventionalMemory 7
#define EfiUnusableMemory 8
#define EfiACPIReclaimMemory 9
#define EfiACPIMemoryNVS 10
#define EfiMemoryMappedIO 11
#define EfiMemoryMappedIOPortSpace 12
#define EfiPalCode 13
#define EfiPersistentMemory 14

typedef struct EFI_MEMORY_DESCRIPTOR {
	UW Type;
	UW Pad;
	EFI_PHYSICAL_ADDRESS PhysicalStart;
	EFI_VIRTUAL_ADDRESS VirtualStart;
	UD NumberOfPages;
	UD Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* ------------------------------------------------------------------ */
/* テキスト出力                                                        */
/* ------------------------------------------------------------------ */

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	VP Reset;
	EFI_STATUS(*OutputString)
	(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CONST EFI_CHAR16 *String);
	VP TestString;
	VP QueryMode;
	VP SetMode;
	VP SetAttribute;
	VP ClearScreen;
	VP SetCursorPosition;
	VP EnableCursor;
	VP Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* ------------------------------------------------------------------ */
/* ブートサービス                                                      */
/*                                                                     */
/* 使うのは AllocatePages / AllocatePool / FreePool / GetMemoryMap /    */
/* HandleProtocol / LocateProtocol / ExitBootServices / Stall だけだが、 */
/* オフセットを合わせるため全メンバを並べる。                           */
/* ------------------------------------------------------------------ */

typedef struct EFI_BOOT_SERVICES {
	EFI_TABLE_HEADER Hdr;

	/* Task Priority Services */
	VP RaiseTPL;
	VP RestoreTPL;

	/* Memory Services */
	EFI_STATUS(*AllocatePages)
	(UD Type, UD MemoryType, UD Pages, EFI_PHYSICAL_ADDRESS *Memory);
	EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, UD Pages);
	EFI_STATUS(*GetMemoryMap)
	(EFI_UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, EFI_UINTN *MapKey,
	 EFI_UINTN *DescriptorSize, UW *DescriptorVersion);
	EFI_STATUS (*AllocatePool)(UD PoolType, UD Size, VP *Buffer);
	EFI_STATUS (*FreePool)(VP Buffer);

	/* Event & Timer Services */
	VP CreateEvent;
	VP SetTimer;
	VP WaitForEvent;
	VP SignalEvent;
	VP CloseEvent;
	VP CheckEvent;

	/* Protocol Handler Services */
	VP InstallProtocolInterface;
	VP ReinstallProtocolInterface;
	VP UninstallProtocolInterface;
	EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, CONST EFI_GUID *Protocol, VP *Interface);
	VP Reserved;
	VP RegisterProtocolNotify;
	VP LocateHandle;
	VP LocateDevicePath;
	VP InstallConfigurationTable;

	/* Image Services */
	VP LoadImage;
	VP StartImage;
	VP Exit;
	VP UnloadImage;
	EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, EFI_UINTN MapKey);

	/* Miscellaneous Services */
	VP GetNextMonotonicCount;
	EFI_STATUS (*Stall)(EFI_UINTN Microseconds);
	VP SetWatchdogTimer;

	/* DriverSupport Services */
	VP ConnectController;
	VP DisconnectController;

	/* Open and Close Protocol Services */
	VP OpenProtocol;
	VP CloseProtocol;
	VP OpenProtocolInformation;

	/* Library Services */
	VP ProtocolsPerHandle;
	VP LocateHandleBuffer;
	EFI_STATUS (*LocateProtocol)(CONST EFI_GUID *Protocol, VP Registration, VP *Interface);
	VP InstallMultipleProtocolInterfaces;
	VP UninstallMultipleProtocolInterfaces;

	/* 32-bit CRC Services */
	VP CalculateCrc32;

	/* Miscellaneous Services */
	VP CopyMem;
	VP SetMem;
	VP CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_CONFIGURATION_TABLE {
	EFI_GUID VendorGuid;
	VP VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct EFI_SYSTEM_TABLE {
	EFI_TABLE_HEADER Hdr;
	EFI_CHAR16 *FirmwareVendor;
	UW FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	VP ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	VP RuntimeServices;
	EFI_BOOT_SERVICES *BootServices;
	EFI_UINTN NumberOfTableEntries;
	EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ------------------------------------------------------------------ */
/* Loaded Image プロトコル — 自分がどのデバイスから読まれたかを知る     */
/* ------------------------------------------------------------------ */

#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                                   \
	{                                                                                    \
		0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }   \
	}

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
	UW Revision;
	EFI_HANDLE ParentHandle;
	EFI_SYSTEM_TABLE *SystemTable;
	EFI_HANDLE DeviceHandle;
	VP FilePath;
	VP Reserved;
	UW LoadOptionsSize;
	VP LoadOptions;
	VP ImageBase;
	UD ImageSize;
	UW ImageCodeType;
	UW ImageDataType;
	VP Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ------------------------------------------------------------------ */
/* Simple File System / File プロトコル — ESP からカーネルを読む        */
/* ------------------------------------------------------------------ */

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                             \
	{                                                                                    \
		0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }   \
	}

#define EFI_FILE_INFO_ID                                                                 \
	{                                                                                    \
		0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }   \
	}

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

struct EFI_FILE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL {
	UD Revision;
	EFI_STATUS(*Open)
	(struct EFI_FILE_PROTOCOL *This, struct EFI_FILE_PROTOCOL **NewHandle,
	 CONST EFI_CHAR16 *FileName, UD OpenMode, UD Attributes);
	EFI_STATUS (*Close)(struct EFI_FILE_PROTOCOL *This);
	VP Delete;
	EFI_STATUS(*Read)
	(struct EFI_FILE_PROTOCOL *This, EFI_UINTN *BufferSize, VP Buffer);
	VP Write;
	VP GetPosition;
	EFI_STATUS (*SetPosition)(struct EFI_FILE_PROTOCOL *This, UD Position);
	EFI_STATUS(*GetInfo)
	(struct EFI_FILE_PROTOCOL *This, CONST EFI_GUID *InformationType,
	 EFI_UINTN *BufferSize, VP Buffer);
	VP SetInfo;
	VP Flush;
} EFI_FILE_PROTOCOL;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	UD Revision;
	EFI_STATUS(*OpenVolume)
	(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct EFI_TIME {
	UH Year;
	UB Month;
	UB Day;
	UB Hour;
	UB Minute;
	UB Second;
	UB Pad1;
	UW Nanosecond;
	H TimeZone;
	UB Daylight;
	UB Pad2;
} EFI_TIME;

typedef struct EFI_FILE_INFO {
	UD Size;
	UD FileSize;
	UD PhysicalSize;
	EFI_TIME CreateTime;
	EFI_TIME LastAccessTime;
	EFI_TIME ModificationTime;
	UD Attribute;
	EFI_CHAR16 FileName[1];
} EFI_FILE_INFO;

/* ------------------------------------------------------------------ */
/* Graphics Output プロトコル (GOP) — 起動時フレームバッファ            */
/* ------------------------------------------------------------------ */

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                                \
	{                                                                                    \
		0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }   \
	}

/* EFI_GRAPHICS_PIXEL_FORMAT */
#define PixelRedGreenBlueReserved8BitPerColor 0
#define PixelBlueGreenRedReserved8BitPerColor 1
#define PixelBitMask 2
#define PixelBltOnly 3

typedef struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
	UW Version;
	UW HorizontalResolution;
	UW VerticalResolution;
	UW PixelFormat;
	UW RedMask;
	UW GreenMask;
	UW BlueMask;
	UW ReservedMask;
	UW PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
	UW MaxMode;
	UW Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	EFI_UINTN SizeOfInfo;
	EFI_PHYSICAL_ADDRESS FrameBufferBase;
	EFI_UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
	/* モードの一覧は読む。**SetMode は呼ばない** —— 詳細 F9 が
	 * 「UEFI が設定したモードをそのまま使う」と決めており、
	 * モード切り替えは実機ブートの前提から外してある。 */
	EFI_STATUS(*QueryMode)
	(struct EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UW ModeNumber, EFI_UINTN *SizeOfInfo,
	 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
	VP SetMode;
	VP Blt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ------------------------------------------------------------------ */
/* Serial I/O プロトコル — 生のバイト列をシリアルポートへ送る           */
/*                                                                     */
/* ConOut は PC-ANSI 端末を仮定して非 ASCII を 8bit に切り捨てるので、   */
/* 日本語を出すにはこちらを使う（src/boot/uefi/console.c の注記）。      */
/* ------------------------------------------------------------------ */

#define EFI_SERIAL_IO_PROTOCOL_GUID                                                      \
	{                                                                                    \
		0xBB25CF6F, 0xF1D4, 0x11D2, { 0x9A, 0x0C, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0xFD }   \
	}

struct EFI_SERIAL_IO_PROTOCOL;

typedef struct EFI_SERIAL_IO_PROTOCOL {
	UW Revision;
	VP Reset;
	VP SetAttributes;
	VP SetControl;
	VP GetControl;
	EFI_STATUS(*Write)
	(struct EFI_SERIAL_IO_PROTOCOL *This, EFI_UINTN *BufferSize, VP Buffer);
	VP Read;
	VP Mode;
} EFI_SERIAL_IO_PROTOCOL;

/* ------------------------------------------------------------------ */
/* コンフィグレーションテーブルの GUID                                 */
/* ------------------------------------------------------------------ */

#define EFI_ACPI_20_TABLE_GUID                                                           \
	{                                                                                    \
		0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }   \
	}

#define EFI_ACPI_10_TABLE_GUID                                                           \
	{                                                                                    \
		0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }   \
	}

#define EFI_DTB_TABLE_GUID                                                               \
	{                                                                                    \
		0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 }   \
	}

#endif /* __BTRON_UEFI_EFI_H__ */
