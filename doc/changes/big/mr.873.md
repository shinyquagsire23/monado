---
- mr.873
- mr.1517
---

New compute based rendering backend in the compositor. Through the layer
squasher it supports both projection and cube layers, not cubemap or equirect
layers. It is not enabled by default. It also supports ATW. On some hardware the
use of a compute queue improves latency when pre-empting other GPU work.
