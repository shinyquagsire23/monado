# Roadmap

## Short term

 * **aux/util**: Add mutex and threading wrappers.
 * **aux/math**: Add kalman filter math black box.
 * **aux/log**: Add a common logging framework that can be used to pipe messages
                up into **st/oxr** from things like drivers and the compositor.
 * **aux/log**: Make it possible to batch up longer messages into a single call,
                useful for printing the entire mode list in a single go.
 * **cmake**: Make a proper FindXCB.cmake file.
 * **comp**: Do timing based of the display refresh-rate and display time.
 * **comp**: Extend to support rotated views/displays. Should we just rotate the
             display for the 3Glasses or make it a per-view thing?
 * **comp**: See-through support for Vive headset.
 * **st/oxr**: Locking, maybe we just have a single lock for the session.
               We will need to figure out how to do wait properly.
 * **st/oxr**: Make wait frame actually wait for the display time.
 * **st/oxr**: Improve space functions.
 * **st/oxr**: Add path functions.
 * **st/oxr**: Add just enough of the action functions to not return errors.

## Long term

 * **aux/beacon**: Complete and integrate Lighthouse tracking code.
 * **comp**: Moving the compositor into it's own process.
 * **comp**: Support quads layers.
 * **comp**: Support other extensions layers.
 * **doc**: Group Related code.
 * **doc**: Lots of documentation for runtime.
 * **drivers**: Port rest of OpenHMD drivers to our runtime.
 * **st/oxr**: Complete action functions.
 * **progs**: Settings and management daemon.
 * **progs**: Systray status indicator for user to interact with daemon.
 * **progs**: Room-scale setup program.
