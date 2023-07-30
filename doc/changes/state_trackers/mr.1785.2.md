---
- mr.1785
- mr.1889
---

OpenXR: Refactor OXR_NO_PRINTING env vars, split them in two (which is useful
for Windows that has stderr and Debug console). And make stderr printing default
off on Windows.
