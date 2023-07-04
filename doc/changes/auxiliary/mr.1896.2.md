a/bindings: Interaction profile inheritance

A requirement of some interaction profile (extensions) specify that some/all
actions must be supported by all other interactions. This commit modifies the
binding generation to support data-inheritance in bindings.json:
* Adds support for profiles in bindings.json to inherit & override other
  profiles.
* Adds a new concept of virtual profiles for profile like extensions
  (e.g. `XR_EXT_palm_pose`) which do not define a profile themselves but
  require their newly defined actions to be supported by all profiles.
* Generates verify bindings functions which only check extensions actions
  only if the extension is enabled.
