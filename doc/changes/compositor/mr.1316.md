c/main: Init comp_base as early as possible, because it needs to be finalised
last in destroy. It's basically a base class and should follow those semantics.
