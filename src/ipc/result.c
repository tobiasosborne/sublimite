#include "ipc/ipc.h"

const char *ipc_result_string(ipc_result result)
{
    switch (result) {
    case IPC_OK: return "success";
    case IPC_EXISTS: return "another instance already owns the IPC endpoint";
    case IPC_INVALID: return "invalid request or IPC endpoint";
    case IPC_LIMIT: return "request or editor capacity limit exceeded";
    case IPC_IO: return "IPC input/output operation failed";
    case IPC_PROTOCOL: return "invalid IPC request or reply";
    case IPC_TIMEOUT: return "running instance did not respond before the timeout";
    case IPC_BUSY: return "running instance is busy";
    case IPC_REJECTED: return "running instance rejected the open request";
    }
    return "unknown IPC error";
}
