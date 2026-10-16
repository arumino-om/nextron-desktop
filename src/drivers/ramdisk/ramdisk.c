/*
 * ramdisk.c — RAM ディスクのデバイスドライバ (周辺核 §1.8)
 *
 * 論理デバイス名 "rda"。ブロック型デバイス、サブユニット 2 つ。
 *
 * ------------------------------------------------------------------------
 * なぜこれを最初のブロックデバイスにするか
 * ------------------------------------------------------------------------
 *
 * ブロック型デバイスの経路（ブロック単位のランダムアクセス、サブユニット
 * = パーティションの切り分け、物理デバイス名による排他）を、実機の
 * ディスクを待たずに通せる。BTRON ファイルシステム (Phase 3) を載せる
 * 最初の媒体にもなる — 仕様の `DK_DISK_RAM` がまさにこれである。
 *
 * ------------------------------------------------------------------------
 * サブユニットの切り分け
 * ------------------------------------------------------------------------
 *
 * "rda" はユニット全体、"rda0" と "rda1" が 2 つのサブユニットである。
 * 仕様「1 つのユニットに含まれるすべてのサブユニットは 1 つのデバイス
 * ドライバで処理される」ので、ドライバはここ 1 つで、サブユニット番号で
 * 先頭ブロックをずらす。
 */

#include <btron/device.h>
#include <btron/memory.h>

#define RAM_BLKSZ 512          /* 1 ブロックのバイト数 */
/* サブユニットあたりのブロック数。
 * 512B × 1024 = 512KB。ファイルシステムを 1 つ載せるのに足りる大きさ。
 * メモリから取るので、実機のディスクが入るまでの暫定。 */
#define RAM_SUB_BLOCKS 1024
#define RAM_SUBS 2             /* サブユニット数 */
#define RAM_BLOCKS (RAM_SUB_BLOCKS * RAM_SUBS)

LOCAL UB *ram_base;

/* サブユニットの先頭ブロック。sub < 0（ユニット全体）なら 0。 */
LOCAL W base_of(W sub)
{
	return (sub < 0) ? 0 : (sub * RAM_SUB_BLOCKS);
}

LOCAL W limit_of(W sub)
{
	return (sub < 0) ? RAM_BLOCKS : RAM_SUB_BLOCKS;
}

LOCAL W ram_read(W sub, W start, B *buf, W size, W *error)
{
	W base;
	W lim;
	W n;
	W i;

	if (error != 0) {
		*error = 0;
	}
	if (start < 0) {
		return ER_DEV; /* 属性データは持っていない */
	}
	if (ram_base == 0) {
		return ER_NOMDA; /* メディアが無い */
	}
	base = base_of(sub);
	lim = limit_of(sub);
	if (start >= lim) {
		return ER_PAR;
	}
	n = (start + size > lim) ? (lim - start) : size;
	/* 仕様「size = 0 の時は実際の読み込みは行わずに、a_size に読み込み
	 * 可能なデータサイズを戻す」 */
	if (size == 0) {
		return lim - start;
	}
	for (i = 0; i < n * RAM_BLKSZ; i++) {
		buf[i] = (B)ram_base[(base + start) * RAM_BLKSZ + i];
	}
	return n;
}

LOCAL W ram_write(W sub, W start, CONST B *buf, W size, W *error)
{
	W base;
	W lim;
	W n;
	W i;

	if (error != 0) {
		*error = 0;
	}
	if (start < 0) {
		return ER_DEV;
	}
	if (ram_base == 0) {
		return ER_NOMDA;
	}
	base = base_of(sub);
	lim = limit_of(sub);
	if (start >= lim) {
		return ER_PAR;
	}
	n = (start + size > lim) ? (lim - start) : size;
	if (size == 0) {
		return lim - start;
	}
	for (i = 0; i < n * RAM_BLKSZ; i++) {
		ram_base[(base + start) * RAM_BLKSZ + i] = (UB)buf[i];
	}
	return n;
}

LOCAL CONST DevDriver ram_drv = {
	"rda",
	DK_DISK_RAM, /* ブロック型（DA_CHARDEV を立てない）、取り外し不可 */
	RAM_SUBS,
	RAM_BLKSZ,
	0,
	0,
	ram_read,
	ram_write,
};

/* 媒体を用意して登録する。**システムメモリから取る** —
 * 仕様 §1.5 の「システム ( OS やデバイスドライバー等 ) からのみ使用可能」
 * がまさにドライバのためのものである。 */
LOCAL Bool ram_registered;

EXPORT ERR ramdisk_register(void)
{
	M_STATE ms;
	VP adr = 0;
	UD bytes = (UD)RAM_BLOCKS * RAM_BLKSZ;
	W nblk;
	ERR er;

	/* 2 度呼ばれても 1 度しか登録しない（serdev と同じ理由）。 */
	if (ram_registered) {
		return ER_OK;
	}
	if (mbk_sts(&ms) < 0 || ms.blksz <= 0) {
		return ER_NOSPC;
	}
	nblk = (W)((bytes + (UD)ms.blksz - 1) / (UD)ms.blksz);
	er = get_mbk(&adr, nblk, M_SYSTEM | M_RESIDENT);
	if (er < 0) {
		return er;
	}
	ram_base = (UB *)adr;
	er = dev_register(&ram_drv);
	if (er == ER_OK) {
		ram_registered = True;
	}
	return er;
}
