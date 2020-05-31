compositor: Remove the `array_size` field from the struct, this was the only
state tracker supplied value that was on the struct, only have values that the
compositor decides over on the struct.
