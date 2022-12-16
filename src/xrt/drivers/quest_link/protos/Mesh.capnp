@0xb76eb3cdb91b2061;

struct MeshVtx
{
    u1 @0 :Float32;
    v1 @1 :Float32;
    u2 @2 :Float32;
    v2 @3 :Float32;
}

struct PayloadRectifyMesh
{
    meshId @0 :UInt32;
    inputResX @1 :UInt32;
    inputResY @2 :UInt32;
    outputResX @3 :UInt32;
    outputResY @4 :UInt32;
    unk2p1 @5 :UInt32;

    vertices @6 :List(MeshVtx);
    indices @7 :List(UInt16);
}