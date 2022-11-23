main: Introduce `comp_target_factory` this struct allows use to remove long and
clumbersome switch statements for each type. Instead the code is generic and
tweaks for specific target types can be reused for others more easily with this
data driven design of the code.
