server: Almost completely overhaul the handling of swapchain life cycle
including: correctly track which swapchains are alive; reuse ids; enforce the
maximum number of swapchains; and destroy underlying swapchains when they are
destroyed by the client.
