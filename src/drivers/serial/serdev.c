/*
 * serdev.c — シリアル回線のデバイスドライバ (周辺核 §1.8)
 *
 * 論理デバイス名 "com"。文字型デバイス。
 *
 * 【規律 R13】ドライバは原則ユーザランドに置くが、**シリアルは例外**で
 * ある。これは起動初期から緊急画面まで動いていなければならない唯一の
 * 出力路で、プロセスもメッセージも立っていない時点から使われる
 * （`hal_console_puts` が `boot_early` から呼ばれている）。
 * ユーザランドのサーバに出せない。詳細 F1 の例外基準に当たる。
 *
 * 【入力はまだ無い】HAL に UART の受信路がない。仕様の文字型デバイスの
 * 規定（D_NOWAIT なら得られた分だけ読む）を満たすには受信割込みか
 * ポーリングが要るので、`read` を置かずに `ER_DEV` を返させている。
 * 読めないデバイスを「読めたふりをして 0 バイト返す」のは嘘になる。
 */

#include <btron/device.h>
#include <btron/hal.h>

LOCAL W ser_write(W sub, W start, CONST B *buf, W size, W *error)
{
	W i;

	(void)sub;
	if (error != 0) {
		*error = 0;
	}
	/* 属性データ (start < 0) は持っていない。 */
	if (start < 0) {
		return ER_DEV;
	}
	for (i = 0; i < size; i++) {
		hal_console_putc(buf[i]);
	}
	return size;
}

LOCAL CONST DevDriver ser_drv = {
	"com",
	DA_CHARDEV | DK_UNDEF,
	0,  /* サブユニットなし */
	-1, /* 文字型なのでブロックサイズは不明 */
	0,  /* open は不要 */
	0,  /* close も不要 */
	0,  /* read は無い。読み出しは ER_DEV になる */
	ser_write,
};

LOCAL Bool registered;

EXPORT ERR serdev_register(void)
{
	/* **2 度呼ばれても 1 度しか登録しない。** 起動列 (D8) が登録し、
	 * 検査も登録を確かめるので、両方から呼ばれる。デバイス管理は
	 * 同名の二重登録を弾かないので、ここで止める。 */
	ERR er;

	if (registered) {
		return ER_OK;
	}
	er = dev_register(&ser_drv);
	if (er == ER_OK) {
		registered = True;
	}
	return er;
}
