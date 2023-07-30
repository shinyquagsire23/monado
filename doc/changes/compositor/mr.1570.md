---
- mr.1684
---

main: Introduce `comp_target_factory`. This struct allows us to remove long and
cumbersome switch statements for each type. Instead the code is generic and
tweaks for specific target types can be reused for others more easily with this
data driven design of the code.
