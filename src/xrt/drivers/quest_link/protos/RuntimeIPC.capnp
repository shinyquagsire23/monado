@0xba804339ecadd443;

struct PayloadRuntimeIPC {
  cmdId @0 :UInt32;
  nextSize @1 :UInt32;
  clientId @2 :UInt32;
  unk @3 :UInt32;
  data @4 :Data;
}