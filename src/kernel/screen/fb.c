/*
 * fb.c — フレームバッファ
 *
 * BootInfo.fb（UEFI GOP 由来。詳細 D8 / F6）を上位半分の MMIO 区画に
 * 写像して描く。
 *
 * 【規律 R18】この写像はカーネルの上位半分にあり、プロセスへの貸出とは
 * 独立に存在し続ける。だから緊急画面が WM の状態に依らず動く（E24）。
 * `hal_vm_map_device()` が返す別名は `BTRON_MMIO_BASE + 物理`で一意なので、
 * 貸出側が別の仮想アドレスに張っても衝突しない。
 *
 * 【画素形式】GOP は BGRA と RGBA の 2 通りを返す（詳細 D8 ⑩）。
 * 色の組み立てを screen_rgb() に閉じて、描画側は形式を意識しない。
 * 規律 R7 の「変換は境界にのみ置く」と同じ形。
 */

#include <btron/bootinfo.h>
#include <btron/screen.h>
#include <btron/vmem.h>

LOCAL Screen scr;
LOCAL Bool ready = False;

EXPORT ERR screen_init(void)
{
	CONST BootFrameBuffer *fb = &bootinfo()->fb;
	UD va;

	if (fb->base == 0 || fb->format == BFB_NONE) {
		/* 画面が無い構成。異常ではない（シリアルだけで動く）。 */
		return E_NOEXS;
	}
	if (fb->width == 0 || fb->height == 0 || fb->pitch < fb->width * 4U) {
		return E_SYS;
	}
	va = hal_vm_map_device(fb->base, fb->size);
	if (va == 0) {
		return E_NOMEM;
	}
	scr.base = va;
	scr.pa = fb->base;
	scr.width = fb->width;
	scr.height = fb->height;
	scr.pitch = fb->pitch;
	scr.format = fb->format;
	ready = True;
	return E_OK;
}

EXPORT CONST Screen *screen(void)
{
	return ready ? &scr : 0;
}

EXPORT UW screen_rgb(UW r, UW g, UW b)
{
	r &= 0xFFU;
	g &= 0xFFU;
	b &= 0xFFU;
	if (!ready || scr.format == BFB_BGRA8) {
		/* 下位から B,G,R,予約 */
		return (r << 16) | (g << 8) | b;
	}
	/* 下位から R,G,B,予約 */
	return (b << 16) | (g << 8) | r;
}

LOCAL inline volatile UW *at(W x, W y)
{
	return (volatile UW *)(VP)(scr.base + (UD)(UW)y * scr.pitch + (UD)(UW)x * 4U);
}

EXPORT void screen_pixel(W x, W y, UW color)
{
	if (!ready || x < 0 || y < 0 || (UW)x >= scr.width || (UW)y >= scr.height) {
		return;
	}
	*at(x, y) = color;
}

EXPORT UW screen_get(W x, W y)
{
	if (!ready || x < 0 || y < 0 || (UW)x >= scr.width || (UW)y >= scr.height) {
		return 0;
	}
	return *at(x, y) & 0xFFFFFFU;
}

EXPORT void screen_fill(W x, W y, W w, W h, UW color)
{
	W iy;
	W ix;

	if (!ready) {
		return;
	}
	/* 画面外にはみ出す分を刈る。呼び出し側に境界検査を書かせない。 */
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > (W)scr.width) {
		w = (W)scr.width - x;
	}
	if (y + h > (W)scr.height) {
		h = (W)scr.height - y;
	}
	for (iy = 0; iy < h; iy++) {
		volatile UW *p = at(x, y + iy);

		for (ix = 0; ix < w; ix++) {
			p[ix] = color;
		}
	}
}

EXPORT void screen_scroll_up(W y, W h, W dy, UW fill)
{
	W words;
	W row;

	if (!ready || dy <= 0 || h <= dy || y < 0) {
		return;
	}
	if (y + h > (W)scr.height) {
		h = (W)scr.height - y;
	}
	words = (W)(scr.width);

	/* 上から順に詰める。読む先が常に書く先より下なので重ならない。 */
	for (row = 0; row < h - dy; row++) {
		volatile UW *dst = at(0, y + row);
		volatile const UW *src = at(0, y + row + dy);
		W i;

		for (i = 0; i < words; i++) {
			dst[i] = src[i];
		}
	}
	screen_fill(0, y + h - dy, (W)scr.width, dy, fill);
}

EXPORT void screen_clear(UW color)
{
	if (!ready) {
		return;
	}
	screen_fill(0, 0, (W)scr.width, (W)scr.height, color);
}
