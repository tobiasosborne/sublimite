# edit-mdv s8: complete reload generation validation

Scope: P4-modules-2 review §1 only.

Expose the file module's existing stat-to-identity, identity-difference and
canonical-entry lstat helpers for savectl. Their comparison behavior is
unchanged, including compatibility with legacy IDs lacking extended metadata.
Newly captured identities retain device, inode, size, mtime, ctime, permission
bits, ownership and entry type. Reload compares the opened descriptor and the
canonical entry before acquisition and again after the copy. An observed
generation change rejects the entire copy and preserves the host's old tree.
Checks and keep operations use the same helpers and retain complete identities.

Journal BASE carries only device/inode/size/mtime, so its post-save cross-check
explicitly compares those fields; it does not masquerade as a complete file ID.
The independently retained save result remains complete.

The regression executable interposes pread only in its own process, delegates
the read to the Linux syscall, then rewrites the actual file through a separate
writable descriptor after the first 1 MiB read. It verifies unchanged size and
restored mtime plus changed ctime before allowing the second read. Additional
coverage checks restored-mtime changes before and after keep adoption.

No allocation or I/O was added to the typing path. Existing optimistic
filesystem validation still has a final-validation-to-publication window;
this slice follows the file module's ctime generation contract rather than
adding an atomic filesystem snapshot facility. Red/green and required build
results are recorded in docs/worker-reports/edit-mdv-s8.md.
