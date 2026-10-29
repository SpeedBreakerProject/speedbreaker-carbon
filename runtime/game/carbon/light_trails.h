// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// NFS: Carbon's nitrous light trails at the length the Xbox 360 draws them, at
// any frame rate: Most Wanted's fix (game/light_trails.h, GitHub issue #10) on
// Carbon's code. game/carbon/light_trails.cpp hooks the game;
// tests/carbon_light_trails_test.cpp checks this header and the shared ring
// logic it uses.
//
// Carbon draws the trails as Most Wanted does, with the car's light flares,
// not with an effect on the picture: a run with its motion blur, its
// streak-flare pass or a particle draw turned off keeps the trails, one with
// the flares' draws skipped loses them. Carbon's RenderFlares (Most Wanted's
// VehicleRenderConn::RenderFlares), sub_825AA5E0, draws each car's flares
// once where the car is, then, in a player view (the view's id at +8 is 1 or
// 2), keeps the car's render matrix and position in a ring of three in its
// CarRenderInfo (the connection's +64): the matrices at +576 (64 bytes each),
// the positions at +768 (16 bytes each, x y z used), the newest one's index
// at +816 and the ring's size at +818 (halfwords; the size is set to 3 when
// the CarRenderInfo is set up, sth at 0x8259FC00). It moves the index and
// writes the newest entry once a call, so once a frame: the matrix with the
// matrix copy sub_8216DB60 (bl at 0x825AA7C4; r3 = the entry, r4 = the
// matrix, r30 = the CarRenderInfo), the position right after it from the
// caller's r1 + 96. Then, while the car's nitrous is on (CarRenderInfo +5084)
// and it moves, it draws its flares 8 times along each of the three segments
// between the entries (oldest to middle, newest to oldest, middle to newest;
// sub_825A3718 with r7 = 8, r9 = 2, r10 = 1), each with the matrix of the
// segment's start: Most Wanted's 24 flares over the car's last two frames.
//
// So the trail spans the last two frames. A real Xbox 360 races Carbon at 30
// fps (the fidelity comparison: the 360's trails reach as far as ours at Frame
// Rate 30, twice as far as ours at 60), so its trail covers 4/60 s of driving;
// at SpeedBreaker's 60 fps it covered 2/60 s: half as long.
//
// The fix is Most Wanted's: at the ring write, the middle entry gets the car's
// position and matrix of 2/60 s of game time ago and the oldest those of 4/60
// s ago (game::trails::FillFor on a History of what the game wrote, timed by
// game::FrameClockMs: whole vblanks of game time); the game then writes the
// newest as before. At 30 fps those are the frames the game wrote last and
// the one before (nothing changes); at 60 fps the frames 2 and 4 before this
// one, so nothing is interpolated. A paused game collapses the trail onto the
// car as a 360 does; a car new or back after a gap keeps the game's own entries
// until its history reaches back. NFSMW_LIGHT_TRAILS=0 turns it off (the name
// Most Wanted's switch has).
#pragma once
#include <game/light_trails.h>

#include <cstdint>

namespace game::trails::carbon
{
    // ctx.lr when RenderFlares calls the matrix copy that writes the newest
    // entry (bl 0x8216DB60 at 0x825AA7C4).
    constexpr uint32_t kRingWriteReturn = 0x825AA7C8;
    // The new position, stored right after the copy from the caller's stack.
    constexpr uint32_t kNewPositionOnStack = 96;
    // CarRenderInfo's ring (kMatrixBytes and kPositionBytes as Most Wanted's).
    constexpr uint32_t kMatrices = 576;
    constexpr uint32_t kPositions = 768;
    constexpr uint32_t kNewestIndex = 816;  // halfword
    constexpr uint32_t kRingSize = 818;     // halfword, 3
    constexpr uint32_t kEntries = 3;

    // Where the game writes entry `slot`'s matrix and position.
    constexpr uint32_t MatrixAt(uint32_t info, uint32_t slot) { return info + kMatrices + slot * kMatrixBytes; }
    constexpr uint32_t PositionAt(uint32_t info, uint32_t slot) { return info + kPositions + slot * kPositionBytes; }

    // The ring write as sub_825AA5E0 makes it (index moved, ring of three, the
    // copy aimed at the newest entry's matrix). Anything else: leave it be.
    constexpr bool RingAsRead(uint32_t info, uint32_t newest, uint32_t size, uint32_t destination)
    {
        return size == kEntries && newest < kEntries && destination == MatrixAt(info, newest);
    }
}
