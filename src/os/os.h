/* L0 os: OS boundary (smoke stub; the real syscall seam lands in Wave 1). */
#ifndef OS_H
#define OS_H

/* App identity every OS path / IPC leaf is built from: "sublimite-v2",
 * injected by the Makefile as EDIT_APP_ID, never spelled in src/. */
const char *os_app_id(void);

#endif
