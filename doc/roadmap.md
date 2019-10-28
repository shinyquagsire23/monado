# Roadmap

## Short term

* **aux/log**: Add a common logging framework that can be used to pipe messages
                up into **st/oxr** from things like drivers and the compositor.
* **cmake**: Make a proper FindXCB.cmake file.
* **comp**: Do timing based of the display refresh-rate and display time.
* **comp**: Support quads layers.
* **comp**: Move into own thread.
* **st/oxr**: Locking, maybe we just have a single lock for the session.
               We will need to figure out how to do wait properly.
* **st/oxr**: Complete action functions.

## Long term

* **aux/beacon**: Complete and integrate Lighthouse tracking code.
* **comp**: Moving the compositor into it's own process.
* **comp**: Support other extensions layers.
* **comp**: See-through support for Vive headset.
* **doc**: Group Related code.
* **doc**: Lots of documentation for runtime.
* **drivers**: Port rest of OpenHMD drivers to our runtime.
* **progs**: Settings and management daemon.
* **progs**: Systray status indicator for user to interact with daemon.
* **progs**: Room-scale setup program.
