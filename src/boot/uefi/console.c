/*
 * console.c — ローダのコンソール出力
 *
 * 出力先は 2 系統ある。
 *
 *   EFI_SERIAL_IO_PROTOCOL   生のバイト列をシリアルポートへ送る。
 *                            UTF-8 がそのまま通る。こちらを優先する。
 *   EFI_SIMPLE_TEXT_OUTPUT   CHAR16 (UTF-16) を受け取る「テキストコンソール」。
 *
 * 後者は使えるなら楽だが、**日本語が出せない**。ファームウェアの端末
 * ドライバは PC-ANSI 端末を仮定していて、ASCII にない文字を
 * `(char)unicode` で 8bit に切り捨てて送ってくる（実測: 「なし」が "jW" に
 * なった = U+306A/U+3057 の下位バイト）。
 *
 * そこで Serial I/O が取れればそれを使い、無ければ ConOut に落とす。
 * カーネルは自前の UART ドライバを持っているので、この問題は
 * ローダの数行だけの話で済む。
 */

#include "efi.h"
#include "loader.h"

LOCAL EFI_SERIAL_IO_PROTOCOL *serial;
LOCAL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout;

EXPORT void lcon_init(VP systab)
{
	CONST EFI_GUID serial_guid = EFI_SERIAL_IO_PROTOCOL_GUID;
	EFI_SYSTEM_TABLE *st = (EFI_SYSTEM_TABLE *)systab;
	VP p = 0;

	conout = st->ConOut;
	serial = 0;
	if (st->BootServices->LocateProtocol(&serial_guid, 0, &p) == EFI_SUCCESS) {
		serial = (EFI_SERIAL_IO_PROTOCOL *)p;
	}
}

/* ------------------------------------------------------------------ */
/* Serial I/O 経路 — UTF-8 のまま送る                                  */
/* ------------------------------------------------------------------ */

LOCAL void serial_write(CONST B *p, UD n)
{
	EFI_UINTN len = n;

	if (n == 0) {
		return;
	}
	serial->Write(serial, &len, (VP)(UD)(VP)p);
}

LOCAL void serial_puts(CONST B *s)
{
	CONST B *run = s;

	for (; *s != 0; s++) {
		if (*s != '\n') {
			continue;
		}
		serial_write(run, (UD)(s - run));
		serial_write("\r\n", 2);
		run = s + 1;
	}
	serial_write(run, (UD)(s - run));
}

/* ------------------------------------------------------------------ */
/* ConOut 経路 — UTF-8 を UTF-16 に直す                                */
/*                                                                     */
/* 規律 R7「変換は境界にのみ置く」と同じ形。Phase 7 で書く TRON コード   */
/* との変換層の縮小版でもある。                                         */
/* ------------------------------------------------------------------ */

#define BUFSZ 128
LOCAL EFI_CHAR16 buf[BUFSZ];
LOCAL W n;

LOCAL void flush(void)
{
	if (n == 0) {
		return;
	}
	buf[n] = 0;
	conout->OutputString(conout, buf);
	n = 0;
}

LOCAL void put16(UH c)
{
	if (n >= BUFSZ - 2) {
		flush();
	}
	buf[n++] = c;
}

LOCAL void conout_puts(CONST B *s)
{
	CONST UB *p = (CONST UB *)s;

	while (*p != 0) {
		UW c = *p;
		W extra;

		if (c < 0x80U) {
			extra = 0;
		} else if ((c & 0xE0U) == 0xC0U) {
			c &= 0x1FU;
			extra = 1;
		} else if ((c & 0xF0U) == 0xE0U) {
			c &= 0x0FU;
			extra = 2;
		} else if ((c & 0xF8U) == 0xF0U) {
			c &= 0x07U;
			extra = 3;
		} else {
			p++; /* 不正なバイトは落とす */
			continue;
		}
		p++;
		while (extra-- > 0 && (*p & 0xC0U) == 0x80U) {
			c = (c << 6) | (*p & 0x3FU);
			p++;
		}

		if (c == '\n') {
			put16('\r'); /* UEFI のコンソールは CRLF を期待する */
		}
		if (c < 0x10000U) {
			put16((UH)c);
		} else {
			/* BMP 外はサロゲートペアにする */
			c -= 0x10000U;
			put16((UH)(0xD800U + (c >> 10)));
			put16((UH)(0xDC00U + (c & 0x3FFU)));
		}
		if (c == '\n') {
			flush(); /* 行単位で出す。異常終了しても途中まで読める */
		}
	}
	flush();
}

/* ------------------------------------------------------------------ */
/* 公開インタフェース                                                  */
/* ------------------------------------------------------------------ */

EXPORT void lcon_shutdown(void)
{
	serial = 0;
	conout = 0;
}

EXPORT void lcon_puts(CONST B *s)
{
	if (serial != 0) {
		serial_puts(s);
	} else if (conout != 0) {
		conout_puts(s);
	}
}

EXPORT void lcon_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	B tmp[19];
	W i;

	tmp[0] = '0';
	tmp[1] = 'x';
	for (i = 0; i < 16; i++) {
		tmp[2 + i] = digits[(v >> (UW)(60 - i * 4)) & 0xF];
	}
	tmp[18] = '\0';
	lcon_puts(tmp);
}

EXPORT void lcon_dec(UD v)
{
	B tmp[24];
	W i = 0;
	W j = 0;
	B rev[24];

	do {
		rev[i++] = (B)('0' + (v % 10));
		v /= 10;
	} while (v > 0 && i < (W)sizeof(rev));
	while (i > 0) {
		tmp[j++] = rev[--i];
	}
	tmp[j] = '\0';
	lcon_puts(tmp);
}
