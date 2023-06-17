---
- mr.689
- mr.690
- mr.740
---

OpenXR: ~~Add quirk for UnrealEngine4.27 to disable depth/stencil buffer to work
around a bug where Unreal would forget to call acquire before wait image.~~
This has been fixed in UnrealEngine and is no longer needed.
