client: Respect native compositor's extra usage bits, so we can remove the
hardcoded always sampled bit. This also ensures that images have exactly the
same usages in both the compositor and app.
