/* Zappo X twin: stands in for the RG40XX H's Mali driver (libmali.so.0).
 * Programs built for the handheld link to it; on the twin it just brings
 * in Mesa's EGL and GLES (software rendering), which provide the same calls. */
int zappo_twin_mali_stub;
