@0x88256e6cc96d5d92;

struct PayloadHaptics {
    timestamp @0 :UInt64; # Timestamp?
    inputType @1 :UInt16; # 0x1=left, 0x2=right, 0x3=gamepad
    hapticType @2 :UInt16; # 0=Simple, 1=Buffered, 2=Auto
    dataUnk1p2 @3 :UInt16; # unk, usually 0
    dataUnk1p3 @4 :UInt16; # unk, usually 0
    amplitude @5 :Float32; # unk, usually 0
    poseTimestamp @6 :UInt64; # Timestamp identical to poseTimestamp in Slice, sometimes 0 if hapticType is 0?
    data @7 :Data;
}