math: Correctly validate quaternion using non-squared "length" instead of
squared "length", certain combinations of elements would produce valid regular
"length" but not valid squared ones.
