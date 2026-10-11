/*
 * bytes.h — バイト列からの整数読み出し（カーネル内部）
 *
 * ファームウェアのテーブルは**整列されていない**。ACPI の MADT サブテーブルは
 * 1 バイト境界から始まり、その中に 8 バイト値が入る。構造体を被せて読むと
 * 未整列アクセスになり、アーキによっては落ちる。
 *
 * また FDT はビッグエンディアン、ACPI はリトルエンディアンで、どちらも
 * 内部表現（リトルエンディアン, A4）とは独立に決まっている。
 *
 * この 2 つの理由から、**バイト単位で組み立てる**。
 * これが規律 R7 の言う「変換は境界にのみ置く」の境界そのものである。
 * ここを通った後の値は内部表現であり、以後エンディアンを意識しない。
 */

#ifndef __BTRON_KERNEL_BYTES_H__
#define __BTRON_KERNEL_BYTES_H__

#include <btron/typedef.h>

/* 物理アドレスからの読み出しに使うので volatile を付ける。
 * ファームウェアのテーブルは書き換わらないが、最適化で消されると困る。 */
typedef CONST volatile UB *BytePtr;

LOCAL inline UB rd_u8(BytePtr p, UD off)
{
	return p[off];
}

/* --- リトルエンディアン (ACPI) --- */

LOCAL inline UH rd_le16(BytePtr p, UD off)
{
	return (UH)((UH)p[off] | ((UH)p[off + 1] << 8));
}

LOCAL inline UW rd_le32(BytePtr p, UD off)
{
	return (UW)p[off] | ((UW)p[off + 1] << 8) | ((UW)p[off + 2] << 16) |
	       ((UW)p[off + 3] << 24);
}

LOCAL inline UD rd_le64(BytePtr p, UD off)
{
	return (UD)rd_le32(p, off) | ((UD)rd_le32(p, off + 4) << 32);
}

/* --- ビッグエンディアン (FDT) --- */

LOCAL inline UW rd_be32(BytePtr p, UD off)
{
	return ((UW)p[off] << 24) | ((UW)p[off + 1] << 16) | ((UW)p[off + 2] << 8) |
	       (UW)p[off + 3];
}

LOCAL inline UD rd_be64(BytePtr p, UD off)
{
	return ((UD)rd_be32(p, off) << 32) | (UD)rd_be32(p, off + 4);
}

/* --- 文字列 --- */

LOCAL inline Bool bstr_eq(BytePtr p, UD off, CONST B *s)
{
	UD i = 0;

	for (; s[i] != '\0'; i++) {
		if (p[off + i] != (UB)s[i]) {
			return False;
		}
	}
	return (p[off + i] == 0) ? True : False;
}

/* 先頭 n バイトが s と一致するか（終端を見ない） */
LOCAL inline Bool bstr_eqn(BytePtr p, UD off, CONST B *s, UD n)
{
	UD i;

	for (i = 0; i < n; i++) {
		if (p[off + i] != (UB)s[i]) {
			return False;
		}
	}
	return True;
}

#endif /* __BTRON_KERNEL_BYTES_H__ */
