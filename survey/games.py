"""Per-game survey parameters: which files the survey scripts read and write,
and the image's section ranges. SB_GAME picks the game (mw by default), as
it does for CMake:

    python3 survey/fix_switch_tables.py                  # Most Wanted
    SB_GAME=carbon python3 survey/fix_switch_tables.py   # Carbon

Paths are relative to the repository root (run the scripts from there).
Section ranges come from survey/xexsurvey.cpp's report (game*/survey/report.md).
"""
import os

GAMES = {
    # NFS: Most Wanted (Xbox 360, USA), default.xex SHA-256 aebdf3c1...
    'mw': dict(
        config='config/nfsmw.toml',
        switch_tables='config/switch_tables.toml',
        switch_tables_inline='config/switch_tables_inline.toml',
        survey='game/survey',          # image.bin, switch_tables.raw.toml, recomp.log
        ppc='ppc',
        text=(0x820E0000, 0x828C8BA0),  # .text + the .embsec code sections
        pdata=(0x820B1200, 0x820DD6B8),
        data=[(0x82000600, 0x820B106C), (0x828D0000, 0x82C8CE8C)],  # .rdata, .data
        # fix_switch_tables.py: XenonAnalyse misread code as tables (their
        # "labels" are instruction words).
        drop={0x825725B0, 0x82576FD0},
        # fix_switch_tables.py: tables XenonAnalyse missed.
        add=[
            # 0x825A45D0: lis/addi r12=0x82063BA8; lhzx r0 = half[r5]; r12 = 0x825A45F8 + r0;
            # bctr. No bounds check (r5 is a trusted argument); 18 entries, the next
            # halfword is ASCII ("un"). Offsets relative to 0x825A45F8.
            dict(base=0x825A45D0, r=5, labels=[
                0x825A45F8, 0x825A4618, 0x825A4644, 0x825A465C, 0x825A46C4, 0x825A46E0,
                0x825A4720, 0x825A47C4, 0x825A4848, 0x825A4898, 0x825A48F8, 0x825A492C,
                0x825A4970, 0x825A49A0, 0x825A49CC, 0x825A49F8, 0x825A4A2C, 0x825A47B4]),
        ],
        # leaf_switch_bounds.py: hand-found bounds. Tail-call stubs that end in
        # `b +4` into the next (.pdata) function: without an explicit size
        # XenonRecomp treats the b as internal and swallows the neighbour,
        # including its jump table.
        manual_bounds={
            0x8218C508: 0x10,
            0x822F33D8: 0x28,
        },
    ),
    # NFS: Carbon Collector's Edition (Xbox 360, USA; title 454107EC, media
    # 1F8BEB88), default.xex SHA-256 922c7a1a...
    'carbon': dict(
        config='config/carbon/carbon.toml',
        switch_tables='config/carbon/switch_tables.toml',
        switch_tables_inline='config/carbon/switch_tables_inline.toml',
        survey='game-carbon/survey',
        ppc='ppc-carbon',
        text=(0x82110000, 0x82B35A64),
        pdata=(0x820C9400, 0x82105C40),
        data=[(0x82000600, 0x820C928C), (0x82B40000, 0x8301DDF0)],
        drop=set(),
        add=[
            # Byte-offset tables: lis/addi r12=table; lbzx r0 = byte[rI];
            # r12 = base + r0; mtctr; nop; nop; bctr. No bounds check (a
            # trusted index); XenonAnalyse finds neither. Offsets are bytes
            # from the instruction after the bctr.
            # 0x8268BED0 (leaf, after padding): table 0x82064E50, index r3,
            # 26 entries and 6 zeros to the table's 32-byte end (zero is
            # case 0's code: kept, harmless).
            dict(base=0x8268BED4, r=3, labels=[
                0x8268BEFC, 0x8268BF0C, 0x8268BF14, 0x8268BF24, 0x8268BF2C, 0x8268BF34,
                0x8268BF3C, 0x8268BF34, 0x8268BF3C, 0x8268BF44, 0x8268BF44, 0x8268BF64,
                0x8268BF64, 0x8268BF4C, 0x8268BF44, 0x8268BF64, 0x8268BF64, 0x8268BF5C,
                0x8268BF04, 0x8268BF4C, 0x8268BF1C, 0x8268BF64, 0x8268BF54, 0x8268BF64,
                0x8268BF64, 0x8268BF6C, 0x8268BEFC, 0x8268BEFC, 0x8268BEFC, 0x8268BEFC,
                0x8268BEFC, 0x8268BEFC]),
            # 0x826F8744: table 0x82064EF8, index r10 = r8 & 63; 9 cases and 7
            # zeros (16 bytes; the bytes after aren't multiples of 4, so not
            # offsets).
            dict(base=0x826F8744, r=10, labels=[
                0x826F876C, 0x826F8778, 0x826F8784, 0x826F8790, 0x826F879C, 0x826F87A8,
                0x826F87B4, 0x826F87C0, 0x826F87D0, 0x826F876C, 0x826F876C, 0x826F876C,
                0x826F876C, 0x826F876C, 0x826F876C, 0x826F876C]),
        ],
        # Tail-call stubs that end in `b +4` into the next (.pdata) function,
        # as Most Wanted's: without an explicit size XenonRecomp treats the b
        # as internal and swallows the neighbour, and 4 of its jump tables'
        # cases end up outside the function (0x8222ECD8, 0x8237EDC0).
        manual_bounds={
            0x8222ECC8: 0x10,
            0x8237ED98: 0x28,
        },
    ),
}

NAME = os.environ.get('SB_GAME', 'mw')
if NAME not in GAMES:
    raise SystemExit(f'SB_GAME={NAME}: not one of {", ".join(GAMES)}')
G = GAMES[NAME]
