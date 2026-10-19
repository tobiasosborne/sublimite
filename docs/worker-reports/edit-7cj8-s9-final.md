Implemented the recovery fallback and async temp cleanup. Required builds, sanitizer suites, and release malloc guard passed.

The [report](docs/worker-reports/edit-7cj8-s9.md) contains red/green evidence and remaining limits: crashes before durable publication, mapped files overwritten in place, and unmeasured native benchmark rows.