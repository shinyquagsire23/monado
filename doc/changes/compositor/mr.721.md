multi: Introduce a new multi client compositor layer, this allows rendering code
to be moved from the IPC layer into the compositor, separating concerns. The
main compositor always uses the multi client compositor, as it gives us a async
render loop for free.
