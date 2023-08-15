@0x88256e6cc96d5d92;

struct PayloadAudio {
    dataUnk0 @0 :UInt64; # Timestamp?
    metaUnk1p0 @1 :UInt16; # 0x1, 0x2, 0x3? which controller maybe? 0x3 = gamepad?
    metaUnk1p1 @2 :UInt16; # usually 0x1 or 0x0
    metaUnk1p2 @3 :UInt16; # unk, usually 0
    metaUnk1p3 @4 :UInt16; # unk, usually 0
    metaUnk2p0 @5 :UInt32; # unk, usually 0
    dataUnk2 @6 :UInt64; # Timestamp identical to poseTimestamp in Slice, sometimes 0 if metaUnk1p1 is 0?
    data @7 :List(UInt8);
}