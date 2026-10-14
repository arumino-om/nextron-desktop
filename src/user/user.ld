/*
 * user.ld — ユーザプログラムの配置
 *
 * 再配置をしないので、BTRON_PROC_TEXT_BASE (0x400000) に固定で結合する。
 * **読み出し＋実行の区画と読み書きの区画をページ境界で分ける。**
 * 保護属性はページ単位でしか付けられないので、またがると
 * 「書けるコード」か「実行できるデータ」ができてしまう。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* ここから読み書き。ページ境界に揃える。 */
    . = ALIGN(4096);
    __data_start = .;

    .data : {
        *(.data .data.*)
        *(.got .got.plt)
    }
    __data_end = .;

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
