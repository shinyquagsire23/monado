# Roadmap

## Short term

* **xrt**: Only use time_state struct in , all drivers use native
           clock.
* **aux/log**: Add a common logging framework that can be used to pipe messages
               up into **st/oxr** from things like drivers and the compositor.
* **cmake**: Make a proper FindXCB.cmake file.
* **@ref comp**: Do timing based of the display refresh-rate and display time.
* **@ref comp**: Support quads layers.
* **@ref comp**: Move into own thread.
* **@ref oxr**: Locking, maybe we just have a single lock for the session.
                We will need to figure out how to do wait properly.
* **@ref oxr**: Complete action functions.

## Long term

* **aux/beacon**: Complete and integrate Lighthouse tracking code.
* **@ref comp**: Moving the compositor into it's own process.
* **@ref comp**: Support other extensions layers.
* **@ref comp**: See-through support for Vive headset.
* **doc**: Group Related code.
* **doc**: Lots of documentation for runtime.
* **@ref drv**: Port rest of OpenHMD drivers to our runtime.
* **progs**: Settings and management daemon.
* **progs**: Systray status indicator for user to interact with daemon.
* **progs**: Room-scale setup program.
